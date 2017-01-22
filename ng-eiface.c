/*-
 * The MIT License (MIT)
 * 
 * Copyright (c) 2017 David Marker
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "common.h"

#include <netgraph/ng_bridge.h>
#include <sys/ioctl.h>
#include <net/if_dl.h>

#define	LLNAMSIZ	18

/* FUNCTIONS */

/*
 * return -1 if nothing available or err, otherwise return id that can be used.
 */
int
lowest_hook(int ngs, const char *bridge)
{
	int		rc, idx;
	struct ng_mesg	*resp;
	struct hooklist *hlist;
	struct nodeinfo	*ninfo;

	/* Makefile generates same number strings in LinkMap */
	int		avail[NG_BRIDGE_MAX_LINKS] = {0};

	rc = NgSendMsg(ngs, bridge, NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL, 0);
	if (-1 == rc) return (-1);
	if (-1 == NgAllocRecvMsg(ngs, &resp, NULL)) return (-1);

	hlist = (struct hooklist *) resp->data;
	ninfo = &hlist->nodeinfo;

	/*
	 * because we have no order guarantee we scan through all existing
	 * links to see what is taken. The links are called `link<x>` where
	 * X is a zero based index into LinkMap.
	 */
	for (idx = 0; idx < ninfo->hooks; idx++) {
		struct linkinfo *const link = &hlist->link[idx];
		const char *key = link->ourhook + sizeof("link") - 1;
		sscanf(key, "%d", &rc);
		avail[rc] = 1;
	}	
	free(resp);
	/* accounted for all links */

	/* return first one avaiable or leave result as NULL */
	for (idx = 0; idx < NG_BRIDGE_MAX_LINKS; idx++) {
		if (0 == avail[idx])
			return (idx);
	}
	return (-1);
}


static int
create_eiface(int ngs, const char *bridge, const char *eiface)
{
	int rc, skt;
        char path[NG_PATHSIZ];
	struct ngm_name nm;
	struct ngm_mkpeer mp = {
		.type = "eiface",
		.ourhook = "link", /* must append number! */
		.peerhook = "ether"
	};
	struct ng_mesg	*resp;
	struct nodeinfo *ninfo;
	struct ifreq	ifr;

	rc = lowest_hook(ngs, bridge);
	(void) snprintf(mp.ourhook, sizeof(mp.ourhook), "link%d", rc);
        //(void) strlcat(mp.ourhook, rc, sizeof(mp.ourhook));

        rc = NgSendMsg(ngs, bridge, NGM_GENERIC_COOKIE, NGM_MKPEER,
            &mp, sizeof(mp));
        if (-1 == rc) return (-1);

        /* lookup the name it just got. */
        (void) strlcpy(path, bridge, sizeof(path));
        (void) strlcat(path, mp.ourhook, sizeof(path));

        rc = NgSendMsg(ngs, path, NGM_GENERIC_COOKIE, NGM_NODEINFO, NULL, 0);
        if (-1 == rc) return (-1);

        rc = NgAllocRecvMsg(ngs, &resp, NULL);
        if (-1 == rc) return (-1);

	ninfo = (struct nodeinfo *) resp->data;
        (void) strlcpy(nm.name, eiface, sizeof(nm.name));
	*(nm.name + strlen(nm.name) - 1) = '\0'; /* remove ':' */

	strncpy(ifr.ifr_name, ninfo->name, sizeof(ifr.ifr_name));
	ifr.ifr_data = nm.name;

        (void) strlcpy(path, ninfo->name, sizeof(path));
	(void) strlcat(path, ":", sizeof(path));
	free(resp);

	rc = NgSendMsg(ngs, path, NGM_GENERIC_COOKIE, NGM_NAME, &nm, sizeof(nm));
	if (-1 == rc) return (-1);

	/* rename interface too */
	if (-1 == (skt = socket(AF_LOCAL, SOCK_DGRAM, 0))) return (-1);
	if (-1 == ioctl(skt, SIOCSIFNAME, &ifr)) return (-1);

        (void) close(skt);

	return (0); 
}

static int
destroy_eiface(int ngs, const char *eiface)
{
	int		rc, idx;
	struct ng_mesg	*resp;
	struct hooklist *hlist;
	struct nodeinfo	*ninfo;

	rc = NgSendMsg(ngs, eiface, NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL, 0);
	if (-1 == rc) return (-1);
	if (-1 == NgAllocRecvMsg(ngs, &resp, NULL)) return (-1);

	hlist = (struct hooklist *) resp->data;
	ninfo = &hlist->nodeinfo;

	/* all interfaces can be removed, there is one or zero ... */
	for (idx = 0; idx < ninfo->hooks; idx++) {
		struct linkinfo *const link = &hlist->link[idx];
		rc = NgSendMsg(ngs, eiface, NGM_GENERIC_COOKIE,
		    NGM_RMHOOK, link, sizeof(struct ngm_rmhook));
	}

	rc = NgSendMsg(ngs, eiface, NGM_GENERIC_COOKIE, NGM_SHUTDOWN, NULL, 0);

	return (0);
}

/*
 * A valid mac string is "bb:bb:bb:bb:bb:bb", where b is a char 0-9a-fA-F.
 * Not checking that it is un-used on this system, much less that it isn't
 * in the ARP cache. Besides that cache may be incomplete. Its your job to
 * pick something unique! This just lets us give a useful error when the
 * mac string isn't right.
 */
static int
validate_mac(const char *mac)
{
	const char	*end;

	if (NULL == mac) return (0);

	if (17 != strlen(mac)) return (-1);
	end = mac + 17;

# define MAC_ERR { \
		(void) fprintf(stderr, \
		    ME ": Error: invalid mac address\n" \
		); \
		return (-1); \
	}

	/*
	 * Seems like sscanf could work with a %x, but '0x' will scan to 0.
	 */
#	define HX_CHECK(p) ( \
		'0' == *p || '1' == *p || '2' == *p || '3' == *p || \
		'4' == *p || '5' == *p || '6' == *p || '7' == *p || \
		'8' == *p || '9' == *p || 'a' == *p || 'b' == *p || \
		'c' == *p || 'd' == *p || 'e' == *p || 'f' == *p || \
		'A' == *p || 'B' == *p || 'C' == *p || 'D' == *p || \
		'E' == *p || 'F' == *p \
	)
	while (mac < end) {
		if (!HX_CHECK(mac)) MAC_ERR
		mac++;
		if (!HX_CHECK(mac)) MAC_ERR
		mac++;
		if (mac != end && ':' != *mac) MAC_ERR
		mac++;
	}

	return (0);
}

static int
set_mac(char *name, char *mac)
{
	int			skt;
	struct sockaddr_dl	sdl;
	struct ifreq		ifr;
	struct sockaddr		*sa;
	char			temp[LLNAMSIZ + 1];

	sa = &ifr.ifr_addr;

	temp[0] = ':';
	strcpy(temp + 1, mac);
	sdl.sdl_len = sizeof(sdl);
	link_addr(temp, &sdl);
	if (sdl.sdl_alen > sizeof(sa->sa_data)) {
		errno = EINVAL;  /* malformed mac */
		return (-1);
	}
	sa->sa_family = AF_LINK;
	sa->sa_len = sdl.sdl_alen;
	bcopy(LLADDR(&sdl), sa->sa_data, sdl.sdl_alen);

	if (-1 == (skt = socket(AF_LOCAL, SOCK_DGRAM, 0)))
		return (-1);
	/*
	 * Unfortunately this is one time we don't want ':' on the end of the name.
	 * But for sake of consistent args to functions we will remove it rather than
	 * pass in a different string here.
	 */
	strncpy(temp, name, sizeof(temp));
	*(temp + strlen(temp) - 1) = '\0'; /* remove ':' */
	strncpy(ifr.ifr_name, temp, sizeof(ifr.ifr_name));

	if (-1 == ioctl(skt, SIOCSIFLLADDR, &ifr)) {
		(void) close(skt);
		return (-1);
	}

	(void) close(skt);
	return (0);
}

#define USAGE { \
	(void) fprintf(stderr, \
		"usage: " ME " -c <bridge> <eiface> <mac address>\n" \
		"       " ME " -d <eiface>\n" \
	); \
	exit(-1); \
}


int
main(int argc, char **argv)
{
	int	rc, err, ngskt, cflag, dflag;
	char	*bridge, *eiface, *mac;
	char	ngpath[2][NG_PATHSIZ];

	ngskt = -1;
	cflag = 0;
	dflag = 0;

	/* valid args
	 *	ng-eiface -c brname ifname macaddr
         *      ng-bridge -d ifname
	 */
	if (argc < 3) USAGE;

	if (0 == strcmp(argv[1], "-c")) {
		if (5 != argc) USAGE;
		bridge = argv[2];
		eiface = argv[3];
		mac = argv[4];
		cflag = 1;
	}
	if (0 == strcmp(argv[1], "-d")) {
		if (3 != argc) USAGE;
		bridge = NULL;
		eiface = argv[2];
		mac = NULL;
		dflag = 1;
	}
	if (0 == (cflag | dflag)) {
		(void) fprintf(stderr,
		    ME ": Error: \"%s\" must be \"-c\" or \"-d\"\n\n", argv[1]
		);
		USAGE;
	}

	err = 0;
	VALIDATE_NODE(bridge);
	VALIDATE_NODE(eiface);
	err += validate_mac(mac);
	if (err) {
		(void) fprintf(stderr, "\n");
		USAGE;
	}
	COPY_NAME(eiface, ngpath[0]);
	COPY_NAME(bridge, ngpath[1]);

	/* input valid, no longer give USAGE on error */

	ngskt = create_ng_sock();

	/*
	 * These checks are racy, interface names come and go along with
	 * bridges. But check is useful to give helpful information.
	 * NOTE: named pointers specially so that we can stringify them for
	 *       tests. Not checking just existence, but also that it is the
	 *       right type. That is, `char *bridge`, expected to be of type
	 *       "bridge".
	 */
	err = 0;
	if (cflag) {
		err += NG_EXIST(bridge);
		err += NG_NOTEXIST(eiface);
		if (err) exit(-1);

		if (0 != (rc = create_eiface(ngskt, bridge, eiface))) {
			(void) fprintf(stderr,
			    ME ": Error: failed to create %s eiface\n",
			    eiface
			);
			exit(-1);
		} else {
			/* if changing mac fails user will need to destroy */
			(void) fprintf(stdout,
				ME ": Success: create: %s eiface\n", eiface
			);
		}
		if (0 != (rc = set_mac(eiface, mac))) {
			(void) fprintf(stderr,
			    ME ": Error: failed to set mac %s eiface\n",
			    eiface
			);
		}
	}
	if (dflag) {
		err += NG_EXIST(eiface);
		if (err) exit(-1);

		if (0 != (rc = destroy_eiface(ngskt, eiface))) {
			(void) fprintf(stderr,
			    ME ": Error: failed to destry: %s eiface\n", eiface
			);
			exit(-1);
		} else {
			(void) fprintf(stdout,
			    ME ": Success: destroy: %s eiface\n", eiface
			);
		}
	}

	return (0);
}

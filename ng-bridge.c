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
#include <netgraph/ng_ether.h>

static int
ether_is_connected(int ngs, char *ether)
{
	int		rc, idx;
	struct ng_mesg	*resp;
	struct hooklist *hlist;
	struct nodeinfo	*ninfo;

	rc = NgSendMsg(ngs, ether, NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL, 0);
	if (-1 == rc) return (-1);
	if (-1 == NgAllocRecvMsg(ngs, &resp, NULL)) return (-1);
	
	hlist = (struct hooklist *) resp->data;
	ninfo = &hlist->nodeinfo;

	rc = 0;
	for (idx = 0; idx < ninfo->hooks; idx++) {
		struct linkinfo *const link = &hlist->link[idx];
		if ('\0' != *link->peerhook) rc++;
	}
	free(resp);
	return (rc);
}


/*
 * Connecting an ethernet interface means connecting the lower and upper hooks
 * to the bridge. This does mean it takes up 2 hooks, not just one.
 *
 * This is only called on newly created bridges. This means link0 and link1
 * are free and should be used on the bridge.
 */
int
connect_ether(int ngs, char *bridge, char *ether)
{
	int			rc;
	static const int	mode = 1;
	struct ngm_connect	cn[] = {
		{
#	define UP	0
			.ourhook = "upper",
			.peerhook = "link1"
		},{
#	define LO	1
			.ourhook = "lower",
			.peerhook = "link0"
		}
	};

	strlcpy(cn[LO].path, bridge, sizeof(cn[LO].path));
	strlcpy(cn[UP].path, bridge, sizeof(cn[UP].path));

	/* we need to have the iface in promisc mode */
	rc = NgSendMsg(ngs, ether, NGM_ETHER_COOKIE,
		    NGM_ETHER_SET_PROMISC, &mode, sizeof(mode));
	if (-1 == rc) return (-1); /* must be able to put in this mode */

	/* return positive int ... */
	(void) NgSendMsg(ngs, ether, NGM_GENERIC_COOKIE,
	    NGM_CONNECT, &cn[UP], sizeof(cn[UP]));
	(void) NgSendMsg(ngs, ether, NGM_GENERIC_COOKIE,
	    NGM_CONNECT, &cn[LO], sizeof(cn[LO]));
	return (0);
}


/* netgraph doesn't distinguish between logical and physical */
int
create_bridge(int ngs, char *bridge)
{
	struct ngm_name nm;
	struct ngm_rmhook rm = {
		.ourhook = "link0"
	};
	struct ngm_mkpeer mp = {
		.type = "bridge",
		.ourhook = "lower",
		.peerhook = "link0" /* always starts at 0 -- but nothing connected yet */
	};

	if (-1 == NgSendMsg(ngs, ".:", NGM_GENERIC_COOKIE, NGM_MKPEER, &mp, sizeof(mp)))
		return (-1);

	/*
	 * Unfortunately this is one time we don't want ':' on the end of the name.
	 * But for sake of consistent args to functions we will remove it rather than
	 * pass in a different string here.
	 */
	(void) strlcpy(nm.name, bridge, sizeof(nm.name));
	*(nm.name + strlen(nm.name) - 1) = '\0'; /* remove ':' */
	if (-1 == NgSendMsg(ngs, ".:lower", NGM_GENERIC_COOKIE, NGM_NAME, &nm, sizeof(nm)))
		return (-1);

	/* need to set NGM_BRIDGE_SET_PERSISTENT so it stays! */
	if (-1 == NgSendMsg(ngs, bridge, NGM_BRIDGE_COOKIE, NGM_BRIDGE_SET_PERSISTENT, NULL, 0))
		return (-1);

	/* this socket is connected to link0, disconnect from it */
	if (-1 == NgSendMsg(ngs, bridge, NGM_GENERIC_COOKIE, NGM_RMHOOK, &rm, sizeof(rm)))
		return (-1);

	return (0);
}


/*
 * Any connected nodes are removed. For eiface this would be like having the
 * cat5 pulled out. They will still need `ng-eiface -d` to destroy them.
 */
int
destroy_bridge(int ngs, char *bridge)
{
	int		rc, idx;
	struct ng_mesg	*resp;
	struct hooklist *hlist;
	struct nodeinfo *ninfo;
	
	rc = NgSendMsg(ngs, bridge, NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL, 0);
	if (-1 == rc) return (-1);
	if (-1 == NgAllocRecvMsg(ngs, &resp, NULL)) return (-1);
	
	hlist = (struct hooklist *) resp->data;
	ninfo = &hlist->nodeinfo;

#if	0
	/* this was checked in ng-bridge.c already ... */
	if (0 != strcmp(ninfo->type, "bridge")) {
		free(resp);
		return (-1);
	}
#	endif


	/* all interfaces can be removed, if link0 is ether then set promis off */
	for (idx = 0; idx < ninfo->hooks; idx++) {
		struct linkinfo *const link = &hlist->link[idx];
		char *type = link->nodeinfo.type;
		/*
		 * going to cheat, we can cast link to be an ngm_rmhook
		 * because all an ngm_rmhook has is the ourhook char array
		 * and that is the first thing in a linkinfo
		 */
		rc = NgSendMsg(ngs, bridge, NGM_GENERIC_COOKIE,
		    NGM_RMHOOK, link, sizeof(struct ngm_rmhook));

		if ((0 == strcmp(type, "ether")) &&
		    (0 == strcmp(link->ourhook, "link0"))) {
			int prom = 0;
			char *name = link->nodeinfo.name;
			char path[NG_PATHSIZ];

			(void) strlcpy(path, name, sizeof(path));
			(void) strlcat(path, ":", sizeof(path));
			/* remove promisc mode */

			rc = NgSendMsg(ngs, path, NGM_ETHER_COOKIE,
				    NGM_ETHER_SET_PROMISC, &prom, sizeof(prom));
		}
	}
	free(resp);

	/* send shutdown for our bridge, since we set persist */
	rc = NgSendMsg(ngs, bridge, NGM_GENERIC_COOKIE, NGM_SHUTDOWN, NULL, 0);

	return (0);
}


#define USAGE { \
	(void) fprintf(stderr, \
		"usage: " ME " -c <bridge> [ether]\n" \
		"       " ME " -d <bridge>\n" \
	); \
	exit(-1); \
}

int
main(int argc, char **argv)
{
	int	rc, err, ngskt, cflag, dflag;
	char	*bridge = NULL;
	char	*ether = NULL;
	char	ngpath[2][NG_PATHSIZ];

	err = 0;
	ngskt = -1;
	cflag = 0;
	dflag = 0;


	/* valid args
	 *	ng-bridge -c bridge
	 *	ng-bridge -c bridge ether
         *      ng-bridge -d bridge
	 */
	if (argc < 3) USAGE;

	if (0 == strcmp(argv[1], "-c")) {
		switch (argc) {
		case 4:
			ether = argv[3]; /* FALLTHROUGH */
		case 3:
			bridge = argv[2];
			break;
		default:
			USAGE;
		}
		cflag = 1;
		
	}
	if (0 == strcmp(argv[1], "-d")) {
		if (3 != argc) USAGE;
		bridge = argv[2];
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
	VALIDATE_NODE(ether);
	if (err) {
		(void) fprintf(stderr, "\n");
		USAGE;
	}

	/*
	 * put the ':' on the end of bridge and ether so it doesn't have to be
	 * done in every function after this.
	 */
	COPY_NAME(bridge, ngpath[0]);
	COPY_NAME(ether, ngpath[1]);


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
		err += NG_NOTEXIST(bridge);
		err += NG_EXIST(ether);
		if (err) exit(-1);

		/* verify ether isn't attached to a bridge already! */
		if (NULL != ether && ether_is_connected(ngskt, ether)) {
			(void) fprintf(stderr,
			    ME ": Error: %s already connected to bridge\n",
			    ether
			);
			exit(-1);
		}
		if (0 != (rc = create_bridge(ngskt, bridge))) {
			(void) fprintf(stderr,
			    ME ": Error: failed to create: %s bridge\n", bridge
			);
			exit(-1);
		} else {
			/* if attaching ether fails user needs to know bridge must be destroyed */
			(void) fprintf(stdout,
				ME ": Success: create: %s bridge\n", bridge
			);
		}
		if (NULL == ether) return (0); /* done */
		if (0 != (rc = connect_ether(ngskt, bridge, ether))) {
			(void) fprintf(stderr,
			    ME ": Error: failed to attatch: %s bridge <-> %s ether\n",
			    bridge, ether
			);
			exit(-1);
		} else {
			(void) fprintf(stdout,
			    ME ": Success: attach: bridge %s <-> %s ether\n",
			    bridge, ether
			);
		}
	}
	if (dflag) {
		err += NG_EXIST(bridge);
		if (err) exit(-1);

		if (0 != (rc = destroy_bridge(ngskt, bridge))) {
			(void) fprintf(stderr,
			    ME ": Error: failed to destroy: %s bridge\n", bridge
			);
			(void) fprintf(stderr,
			    "Try `ngctl show %s` to see if eiface attached.\n\n",
			    bridge
			);
			exit(-1);
		} else {
			(void) fprintf(stdout,
			    ME ": Success: destroy: %s bridge\n", bridge
			);
		}
	}

	return (0);

}

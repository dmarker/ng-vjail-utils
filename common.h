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

#ifndef _DMARKER_CMD_H
#define _DMARKER_CMD_H

#include <errno.h>
#include <netgraph.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>

#define STRFY2(x) #x
#define STRFY(x) STRFY2(x)

typedef void (*check_err)(const char *, const char *, const char *);

static inline int
create_ng_sock(void)
{
	int	skt;
	char    name[NG_NODESIZ];

	snprintf(name, sizeof(name), "ngctl%d", getpid());

	if (-1 == NgMkSockNode(name, &skt, NULL)) {
		int err = errno;
		(void) fprintf(stderr,
		    ME ": Error: Failed to initialize netgraph(8)\n%s\n\n",
		    strerror(err)
		);
		exit(-1);
	}
	return skt;
}

static inline int
validate_node(const char *node)
{
	if (NULL == node) return (0);
	if (strlen(node) >= IFNAMSIZ) return (-1);
	/* only allow a-zA-Z0-9 and '-' itself
	 * A-z is (65,122]
	 * 0-9 is (48,57]
	 * - is 45
	 * ':' is a netgraph separator, '[' and ']' indicate IDs in netgraph
	 * and really this is going to be a system interface name so that's
	 * allyou need.
	 */
	while('\0' != *node) {
		if (45 == *node ||
		    (*node >= 48 && *node <= 57) ||
		    (*node >= 65 && *node <= 122)
		) {
			node++;
			continue;
		} else {
			return (-1);
		}
	}
	return (0);
}

/* requires `int err` declared, and you should zero before checking */
#define VALIDATE_NODE(ptr) \
	if (0 != validate_node(ptr)) { \
		err = 1; \
		(void) fprintf(stderr, \
		    ME ": Error: invalid " #ptr \
		    ", must be less than " STRFY(IFNAMSIZ) " characters " \
		    "composed of a-zA-Z0-9 or '-'\n" \
		); \
	}


#define	COPY_NAME(src, dst) \
	if (NULL != src) { \
		(void) strlcpy(dst, src, sizeof(dst)); \
		(void) strlcat(dst, ":", sizeof(dst)); \
		src = dst; \
	}


/* ng_type() returns a char * into Type or NULL
 * These match the names netgraph reports so that we can just do
 * strcmp and return the matching one, when a node is found.
 */

/* helper for ng_check */
static inline const char * const
ng_type(int ngs, const char *node)
{
	int		rc, ix;
	struct ng_mesg	*resp;
	struct nodeinfo *ninfo;
	static const char * const Type[] = {
		"bridge",
		"eiface",
		"ether",
		"unknown",
		"nonexistent"
	};
#	define NTYPE 3
#	define UNKNOWN Type[3]
#	define NONEXISTENT Type[4]

	rc = NgSendMsg(ngs, node, NGM_GENERIC_COOKIE, NGM_NODEINFO, NULL, 0);
	if (-1 == rc) {
		if (ENOENT == errno)
			return (NONEXISTENT); /* no such node */
		else
			return (NULL); /* error */
	}
	if (-1 == NgAllocRecvMsg(ngs, &resp, NULL)) return (NULL);
	ninfo = (struct nodeinfo *) resp->data;

	for (ix = 0; ix < NTYPE; ix++) {
		if (0 == strcmp(Type[ix], ninfo->type)) break;
	}
	free(resp);
	return Type[ix];
}


static inline int
ng_check(int ngskt, const char *node, const char *expected, check_err err)
{
	int		rc;
	const char	*type;

	if (NULL == node)
		return (0);

	type = ng_type(ngskt, node);
	if (NULL == type) {
		(void) perror(ME);
		return (1);
	}
	rc = strcmp(type, expected);
	if (0 != rc) {
		err(node, type, expected);
		return (1);
	}
	return (0);
}

static void
exist_err(const char *node, const char *type, const char *expected)
{
	if (0 == strcmp(type, "nonexistent")) {
		(void) fprintf(stderr,
		    ME ": Error: %s %s doesn't exist\n",
		    node, expected 
		);
	} else {
		(void) fprintf(stderr,
		    ME ": Error: %s type %s expected %s\n",
		    node, type, expected
		);
	}
}


static void
nonexist_err(const char *node, const char *type, const char *unused)
{
	(void) fprintf(stderr,
	    ME ": Error: %s %s already exists\n",
	    node, type
	);
}


#define NG_EXIST(ptr) ng_check(ngskt, ptr, STRFY(ptr), exist_err)
#define NG_NOTEXIST(ptr) ng_check(ngskt, ptr, "nonexistent", nonexist_err)

#endif /*  _DMARKER_CMD_H */

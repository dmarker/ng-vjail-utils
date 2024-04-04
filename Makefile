#-
# The MIT License (MIT)
# 
# Copyright (c) 2017 David Marker
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# 


CC=/usr/bin/clang
INSTALL=/usr/bin/install
RM=/bin/rm

CFLAGS=-std=c99 -g -Wall -Werror

OBJ_BRIDGE = \
	ng-bridge.o

OBJ_EIFACE = \
	ng-eiface.o

all: ng-bridge ng-eiface

ng-bridge : ng-bridge.o
	$(CC) -o $@ ng-bridge.o -lnetgraph

ng-eiface: ng-eiface.o
	$(CC) -o $@ ng-eiface.o -lnetgraph

# main program compiled with defines of `ME`
ng-bridge.o : ng-bridge.c common.h
	$(CC) $(CFLAGS) -DME=\"ng-bridge\" -c $< -o $@

ng-eiface.o : ng-eiface.c common.h
	$(CC) $(CFLAGS) -DME=\"ng-eiface\" -c $< -o $@

install: ng-bridge ng-eiface netgraph
	$(INSTALL) -o root -g wheel -m 755 -d /usr/local/etc/rc.d
	$(INSTALL) -o root -g wheel -m 555 netgraph /usr/local/etc/rc.d
	$(INSTALL) -o root -g wheel -m 755 -d /usr/local/bin
	$(INSTALL) -o root -g wheel ng-bridge /usr/local/bin
	$(INSTALL) -o root -g wheel ng-eiface /usr/local/bin
	echo "You must alter /etc/rc.d/netif to depend on netgraph"

.PHONY:
clean:
	$(RM) -f *.o

.PHONY:
clobber: clean
	$(RM) -f ng-bridge ng-eiface

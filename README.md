# ng-vjail-utils
Utilities to use [Netgraph](https://people.freebsd.org/~julian/netgraph.html) with VIMAGE/VNET jails in [FreeBSD](https://www.freebsd.org).
NOTE: this has been updated in anticipation of this [PR](https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=278130) with this [review](https://reviews.freebsd.org/D44615) being merged.

### Utilities Provided
 - ng-bridge: create/destroy a physical or logical bridge
 - ng-eiface: create an eiface and add to bridge or destroy an eiface

### Prerequisites
Ability to build your own kernel adding these options

```
options         NETGRAPH
options         NETGRAPH_ETHER
options         NETGRAPH_EIFACE
options         NETGRAPH_BRIDGE
options         NETGRAPH_SOCKET
```
 
### Command Summary
```sh
ng-bridge -c <bridge> [ether]
```
creates the ng_bridge
if `ether` is given creates a physical bridge using the interface. This has to be done while interface is down!

```sh
bridge -d <bridge>
```
Destroy a bridge.
Logical or physical this just means removing all connections, ether or eiface.
It does not destroy the ether or eiface connected to it.

This is highly destructive. Network connections will be destroyed.

```sh
ng-eiface -c <bridge> <eiface> <mac address>
```
Create an eiface and connect it to bridge.
Names must be unique across system, not just for the bridge.
Netgraph may not care, but ifconfig would be confused if we allowed two or more 'eth0' for example.
Also don't want an eiface to have the same name as real device.

```sh
ng-eiface -d <eiface>
```
Remove eiface from bridge and destroy it.

### Notes
A physical bridge has its first two links of type `ether` not `eiface`. That is just my convention. A logical bridge doesn't have any `ether` connected and is like a `host-only` network. This can be useful so that your jails have a private network to connect to a database for example.

Not going to rename ether, that can be done in `/etc/rc.conf`, e.g.
```sh
ifconfig_hn0_name=lan26
```
Ether is put in promiscuous mode (because it must be) when creating a bridge with an `ether`. Promiscuous is turned off when the bridge is destroyed.

These utilities do *nothing* you can't already do with ngctl(8) and ifconfig(8). For a logical bridge you would have to create a file or run ngctl(8) with interactive mode. But it is perfectly capable.

To attach a new eiface you would need to connect it using `link` which will pick the lowest available `linkX` for the bridge. Additionally you would change its name with both ngctl(8) and ifconfig(8).

These utilities are just providing some simplicity. That simplicity in turn makes it easy to provide /usr/local/etc/rc.d/netgraph for setting up netgraph logical or physical bridges and eifaces at boot (as well as removal at shutdown). It is also trivial to use ng-eiface in /etc/jail.conf to create then gift an interface to a jail.

For example your /etc/rc.conf could have

```
ifconfig_re0="inet 192.168.64.26 netmask 255.255.255.0"
defaultrouter="192.168.64.1"

netgraph_enable="YES"
ngbridge_re0="bridge-lan"
ngbridge_lg0="bridge-jail"
# jail0 is not gifted to a jail it is for host system to use bridge-jail as network with jails.
ngeiface_jail0="bridge-jail 00:0C:29:C3:72:F9"
ifconfig_jail0="inet 10.10.0.26 netmask 255.255.255.0"

```
In this case I'm using a realtek network adapter. You must be able to set the interface into promiscuous mode. Not all physical drivers play perfectly with ng_bridge(4). In particular the realtek with releng/14 is able to ping jails connected to bridge-lan while stable/14 requires the realtek driver from ports and it is not able to ping jails connected to bridge-lan. But this is another reason to have bridge-jail.


Your /etc/jail.conf could have
```
somejail {
  vnet;
  vnet.interface = lan30, jail30;
  exec.prestart = "/usr/local/bin/ng-eiface -c bridge-lan lan30 00:15:5d:01:11:30";
  exec.prestart += "/usr/local/bin/ng-eiface -c bridge-lan jail30 00:0C:29:39:B4:4C";
  exec.start = "/bin/sh /etc/rc";
  exec.stop = "/bin/sh /etc/rc.shutdown";
  exec.poststop += "/bin/sleep 2";
  exec.poststop += "/usr/local/bin/ng-eiface -d lan30";
  exec.poststop += "/usr/local/bin/ng-eiface -d jail30";
  ...
}
```

It is vital to use comma for multiple `vnet.interface` and not quote them. You will get a panic on jail shutdown if you don't have the `sleep 2`.

Netgraph ng_eiface(4) in a jail can be configured exactly as if it were a device on the system (say em0). It can use DHCP to get an IP address and set resolv.conf but that requires two things.

First you must have added this (or similar to unhide `bpf` for DHCP) to /etc/devfs.rules:
```
[devfsrules_jail_vnet_dhcp=6]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add include $devfsrules_jail
add path pf unhide
add path 'bpf*' unhide
```

Next, you have to remove the `nojail` and `novjailvnet` keywords from three rc scripts in the jail:
- /etc/rc.d/defaultroute
- /etc/rc.d/netif
- /etc/rc.d/routing

With the netgraph interface, all of those scripts work just fine. You should be aware that DHCP requiring bpf is not secure. It means any jail gets total access to the physical network device. You are better off manually setting IPv4 addresses or using IPv6 (but not DHCP). You must alter those 3 scripts even if not using DHCP for the networking to configure.

It used to be that you had to attach a physical ethernet to a bridge before bringing it up, that doesn't appear true anymore.
But I still have `/etc/rc.d/netif` depend on `netgraph` which `make install` should have put into your `/usr/local/etc/rc.d`.

### TODO
/usr/loca/etc/rc.d/netgraph is really bare bones.

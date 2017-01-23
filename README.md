# ng-vjail-utils
Utilities to use [Netgraph](https://people.freebsd.org/~julian/netgraph.html) with VIMAGE/VNET jails in [FreeBSD](https://www.freebsd.org).

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

To attach a new eiface you would need to scan the list of connected hooks, and pick one out that is available. Additionally you would change its name with both ngctl(8) and ifconfig(8).

These utilities are just providing some simplicity. That simplicity in turn makes it easy to provide /usr/local/etc/rc.d/ngbr for setting up netgraph logical and physical bridges at boot (as well as removal at shutdown). It is also trivial to use ng-eiface in /etc/jail.conf to create then gift an interface to a jail.

For example your /etc/rc.conf could have

```
ifconfig_hn0="inet 192.168.64.26 netmask 255.255.255.0"
defaultrouter="192.168.64.1"

netgraph_enable="YES"
ngbridge_hn0="bridge-lan"
ngbridge_lg0="bridge-jail"
ngeiface_jail26="bridge-jail 00:0C:29:C3:72:F9"
ifconfig_jail26="inet 10.10.0.26 netmask 255.255.255.0"

```
In this case I'm using Hyper-V network adapter. This should work fine with VMWare or even a physical interface. But you must be able to set the interface into promiscuous mode. For Hyper-V that requires adding additional permisions outside the guest VM.


Your /etc/jail.conf could have
```
somejail {
  vnet;
  vnet.interface = lan30, jail30;
  exec.prestart = "/usr/local/bin/ng-eiface -c bridge-lan lan30 00:15:5d:01:11:30";
  exec.prestart += "/usr/local/bin/ng-eiface -c bridge-lan jail30 00:0C:29:39:B4:4C";
  exec.start = "/bin/sh /etc/rc";
  exec.stop = "/bin/sh /etc/rc.shutdown";
  exec.poststop += "/usr/local/bin/ng-eiface -d lan30";
  exec.poststop += "/usr/local/bin/ng-eiface -d jail30";
  ...
}
```

It is vital to use comma for multiple `vnet.interface` and not quote them.

Netgraph ng_eiface(4) in a jail can be configured exactly as if it were a device on the system (say em0). It can use DHCP to get an IP address and set resolv.conf.

However, you have to remove the `nojail` and `novjailvnet` keywords from three
rc scripts in the jail:
- /etc/rc.d/defaultroute
- /etc/rc.d/netif
- /etc/rc.d/routing

With the netgraph interface, all of those scripts work just fine.

You need to have `/etc/rc.d/netif` depend on `netgraph` which `make install` should have put into your `/usr/local/etc/rc.d`.

### TODO
/usr/loca/etc/rc.d/netgraph is really bare bones.

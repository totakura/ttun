ttun
====

This is an example to show how to use TUN interfaces in Linux.

To compile you need to have libevent (http://libevent.org/) installed.

The simple way to understand how this works is to test ttun on two hosts
connected in a network:
1. Create TUN devices on each host:
   $ ip tuntap add dev tun0 mode tun [user <username>]

   `user <username>' is optional.  Do so helps to run ttun without special
   privileges.

2. Assign IP address to the TUN interfaces and bring them up.  Choose the IPs
   from the same subnet which should be different any subnets which you already use:
    $ ip addr add 10.x.x.x/24 dev tun0
    $ ip link set dev tun0 up

3. Start ttun instances on each host like this:
   $ ttun tun0 <host1_IP> 8000 <host2_IP> 8000

   Interchange the IP addresses for the other host.  The first IP address is the
   one used by ttun to bind a UDP socket and receive UDP packets from the other
   ttun instance.  The second IP address is the one to which ttun should send
   UDP packets to.

4. Try pinging the IP assigned to the local TUN interface; it should succed.  Try
   pinging the IP assigned to the TUN interface on other host.  It should
   succeed too and you should now be able to see the debug output from ttun.

5. Start netcat on one of the hosts and bind it to a port on the local TUN interface IP:
   $ nc -l -s <TUN_IP> -p 8000
   The options for netcat may vary depending on the netcat installed

6. From the other host open a netcat connection to the previous
   instance through the local TUN
   $ nc TUN_IP 8000

   Here, TUN_IP is the same of both hosts.  It is assigned to the local TUN
   interface on the first host.  The idea is that since this IP and the local
   TUN interface IP on the second host fall into the same subnet, any packets
   destined to that IP will be sent via the TUN interface.  The ttun instance on
   the second host will then read those packets, send them encapsulated in UDP
   packets to the ttun instance on the first host which will extract the packets
   and writes to its local TUN interface.  Relpies are processed vice-versa.

This is a setup to test ttun on a single host.  This requires usage of network
namespaces, virtual ethernet interfaces and ethernet bridging.  Most of these
capabilities are available in modern GNU/Linux operating systems.  This section
is INCOMPLETE.

1. Create virtual ethernet interfaces
   $ ip link add type veth

   The above command will create two interfaces: veth0, veth1.  The underlying
   concept of virtual ethernet interfaces is that they act like a pipe, anything
   written at an end will appear at the other end, just like tunnels.

2. We will use network name spaces two create two sites A and B which are
   connected to each other.
   $ ip netns add siteA
   $ ip netns add siteB

3. Add the virtual ethernet interface to each network namespace
   $ ip link set dev veth0 netns siteA
   $ ip link set dev veth1 netns siteB

4. Create a TUN devices in each network namespace:
   $ ip netns exec siteA ip tuntap add dev tun0 mode tun

   Do the same for siteB.

5. Assign ip addresses to the virtual ethernet interfaces and TUN interfaces in
   the namespaces:
   $ ip netns exec siteA ip addr add 10.0.1.1/24 dev veth0
   $ ip netns exec siteB ip addr add 10.0.1.2/24 dev veth1
   $ ip netns exec siteA ip addr add 10.0.2.1/24 dev tun0
   $ ip netns exec siteB ip addr add 10.0.2.2/24 dev tun0

   Notice that the veth interfaces in both namespace should be in the same
   subnet.  Similary, the TUN interfaces in both namespaces should also be in
   the same subnet different from the subnet used for the veth interface.

6. Bring all the veth and TUN interfaces up
   $ ip netns exec siteA ip link set dev veth0 up
   $ ip netns exec siteA ip link set dev tun0 up
   $ ip netns exec siteB ip link set dev tun0 up
   $ ip netns exec siteB ip link set dev veth1 up

7. Also bring up the loopback interfaces up so that we can ping local interface
   IPs:
   $ ip netns exec siteA ip link set dev lo up
   $ ip netns exec siteB ip link set dev lo up

   Now you can ping local interfaces IP addresses: ip netns exec siteA ping
   10.0.1.1
   You should also be able to ping the veth of siteA from siteB and vice-versa:
   $ ip netns exec siteB ping 10.0.1.1
   $ ip netns exec siteA ping 10.0.1.2

   However, ping from siteB to siteA's TUN interface IP should not work.
   $ ip netns exec siteB ping 10.0.2.1 #should fail
   $ ip netns exec siteA ping 10.0.2.2 #should fail

8. Start a ttun instance in each of the namespaces:
   1. ip netns exec siteA ./ttun tun0 10.0.1.1 9000 10.0.1.2 9000
   2. ip netns exec siteB ./ttun tun0 10.0.1.2 9000 10.0.1.1 9000

   Now, ping from siteB to siteA's TUN interface IP should work.
   $ ip netns exec siteB ping 10.0.2.1 #should work
   $ ip netns exec siteA ping 10.0.2.2 #should work

9. You may now run netcat on of the sites and connect to it from the other site
   using the tunnel:
   $ ip netns exec siteB nc -l -s 10.0.2.2 -p 8000 #nc listens for incoming connections
   $ echo "Hello World" | ip netns exec siteA nc 10.0.2.2 8000

   Now the first command should print "Hello World".

10. Delete the network namespaces:
    $ ip netns delete siteA
    $ ip netns delete siteB
   

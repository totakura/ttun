/* Creates/opens a TUN interface and relays data to a given end point.

For creating TUN interfaces, root privileges are needed.  They are not needed
for using it, if the TUN interface has sufficient access rights.

On GNU/Linux the ip command can be used to create a TUN device:
$ sudo ip tuntap add tun0 mode tun user <username> group <groupname>

The above command creates a TUN device with the name tun0 and assigns the user
<username> and group <groupname> to able to use it for reading/writing.
*/


#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <event.h> //pass -levent_core linker flag
#include <linux/if.h>
#include <linux/if_tun.h>

#define LOG(...)                                \
  (void) fprintf (stderr, __VA_ARGS__)

/* The signal on which we bail out */
#define SIG SIGINT

/* MTU associated without interfaces */
#define MTU 1500

/* main event loop */
static struct event_base *ev_base;

/* events for reading and writing TUN interface */
static struct event *ev_tread;
static struct event *ev_twrite;

/* events for sending and receiving from UDP socket */
static struct event *ev_swrite;
static struct event *ev_sread;

/* Descriptors the TUN interface and UDP socket */
static int tun;
static int sock;

struct Buffer
{
  char data[MTU];
  size_t size;
  size_t off;
};

/* buffer for data read from TUN */
static struct Buffer in;

/* buffer for data wrote to TUN */
static struct Buffer out;

/* Catch a signal and quit the event loop */
static void
event_sig_cb (evutil_socket_t fd, short flags, void *cls)
{
  int sig = fd;

  assert (SIG == sig);
  LOG ("Exiting..\n");
  event_base_loopexit (ev_base, NULL);
}

/* read from the TUN and schedule a write to the sock */
static void
ev_tread_cb (evutil_socket_t tun, short flags, void *cls)
{
   int nread;

  assert (NULL != ev_tread);
  assert (NULL == cls);
  assert (0 == (EV_TIMEOUT & flags));
  assert (0 != (EV_READ & flags));

  nread = read (tun,
                &in.data[in.off],
                sizeof (in.data)-in.off);
  assert (nread > 0);
  assert (nread <= MTU);
  LOG ("Read %d bytes\n", nread);
  fflush (stderr);
  in.size = nread;
  /* write to the socket */
  event_add (ev_swrite, NULL);
}

/* read from  */
static void
ev_swrite_cb (evutil_socket_t fd, short flags, void *cls)
{
  struct Buffer *buf = cls;

  assert (sock == fd);
  assert (NULL != ev_swrite);
  assert (NULL != buf);
  assert (0 == (EV_TIMEOUT & flags));
  assert (0 != (EV_WRITE & flags));
  assert (-1 != send (sock, &buf->data[buf->off], buf->size, 0));
  /* resume reading from TUN */
  event_add (ev_tread, NULL);
}


/* Write the data to the TUN interface and schedule a read on the sock */
static void
ev_twrite_cb (evutil_socket_t fd, short flags, void *cls)
{
  struct Buffer *buf = cls;
  int nwrote;

  assert (tun == fd);
  assert (NULL != ev_twrite);
  assert (NULL != buf);
  assert (0 == (EV_TIMEOUT & flags));
  assert (0 != (EV_WRITE & flags));

  nwrote = write (tun,
                  &buf->data[buf->off],
                  buf->size);
  assert (nwrote >= 0);
  /* resume reading from socket */
  event_add (ev_sread, NULL);
}

/* read data from the sock and schedule a write to TUN */
static void
ev_sread_cb (evutil_socket_t tun, short flags, void *cls)
{
  int nread;

  assert (NULL != ev_sread);
  assert (NULL == cls);
  assert (0 == (EV_TIMEOUT & flags));
  assert (0 != (EV_READ & flags));

  nread = recv (sock,
                &out.data[out.off],
                sizeof (out.data)-out.off,
                0);
  assert (nread > 0);
  assert (nread <= MTU);
  LOG ("Read %d bytes from socket\n", nread);
  fflush (stderr);
  out.size = nread;
  /* write to the socket */
  event_add (ev_twrite, NULL);
}

/* Opens the TUN device so that we can read/write to it.  If we do not have
   CAP_SYS_NETADMIN capability, we are restricted to use the TUN device
   allocated for us */
static int
open_tun (char *dev)
{
  struct ifreq ifr;
  int fd, err;

  if( (fd = open("/dev/net/tun", O_RDWR)) < 0 )
    return fd;

  memset(&ifr, 0, sizeof(ifr));

  /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
   *        IFF_TAP   - TAP device
   *
   *        IFF_NO_PI - Do not provide packet information
   */
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if( *dev )
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);

  if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ){
    close(fd);
    return err;
  }
  strcpy(dev, ifr.ifr_name);
  return fd;
}

/* creates and configures an UDP socket to the tunnel end point */
static int
create_udpsock (const char *ipstr, const char *portstr)
{
  struct sockaddr_in addr;
  uint16_t port;
  int sock;
  int flags;
  int ret;

  if (1 != sscanf (portstr, "%hu", &port))
  {
    LOG ("Invalid port: %s\n", portstr);
    return -1;
  }
  (void) memset (&addr, 0, sizeof (addr));
  ret = inet_pton (AF_INET, ipstr, &addr.sin_addr);
  if (0 == ret)
  {
    LOG ("Invalid IP address: %s\n", ipstr);
    return -1;
  }
  if (-1 == ret)
  {
    LOG ("AF_INET address family not supported on this system.\n");
    return -1;
  }
  assert (1 == ret);
  sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0)
    return sock;
  addr.sin_port = htons (port);
  /* connect the UDP socket so that we can use send/recv with UDP sockets */
  ret = connect (sock, (const struct sockaddr *) &addr, sizeof (addr));
  if (0 != ret)
  {
    (void) close (sock);
    return -1;
  }
  /* make socket non blocking */
  flags = fcntl (sock, F_GETFL);
  assert (0 <= flags);
  flags |= O_NONBLOCK;
  ret = fcntl (sock, F_SETFL, flags);
  assert (-1 != ret);
  return sock;
}

int
main (int argc, const char *argv[])
{
  struct event_config *ev_cfg;
  struct event *event_sig;
  char *tun_name;

  if (argc < 4)
  {
    LOG("ttun-create tun_dev IP port\n");
    return 1;
  }
  tun = -1;
  sock = -1;
  event_enable_debug_mode ();
  ev_cfg = event_config_new ();
  assert (NULL != ev_cfg);
  assert (0 == event_config_require_features (ev_cfg, EV_FEATURE_O1));
  assert (0 == event_config_require_features (ev_cfg, EV_FEATURE_FDS));
  ev_base = event_base_new_with_config (ev_cfg);
  assert (NULL != ev_base);
  event_sig = evsignal_new (ev_base, SIG, &event_sig_cb, NULL);
  assert (0 == evsignal_add (event_sig, NULL));

  sock = create_udpsock (argv[2], argv[3]);
  if (-1 == sock)
    goto cleanup;
  tun_name = strdup (argv[1]);
  tun = open_tun (tun_name);
  assert (tun >= 0);
  free (tun_name);

  ev_tread = event_new (ev_base,
                        tun,
                        EV_READ,
                        &ev_tread_cb, NULL);
  ev_sread = event_new (ev_base,
                        sock,
                        EV_READ,
                        &ev_sread_cb, NULL);
  ev_twrite = event_new (ev_base,
                         tun,
                         EV_WRITE,
                         &ev_twrite_cb, &in);
  ev_swrite = event_new (ev_base,
                         sock,
                         EV_WRITE,
                         &ev_swrite_cb, &out);
  assert (NULL != ev_tread);
  assert (0 == event_add (ev_tread, NULL));
  assert (0 == event_add (ev_sread, NULL));
  /* Don't add the write event now, we need them when there is data */
  /* assert (0 == event_add (ev_twrite, NULL)); */
  /* assert (0 == event_add (ev_swrite, NULL)); */
  assert (0 == event_base_dispatch (ev_base));

 cleanup:
  if (-1 != tun)
    (void) close (tun);
  event_free (event_sig);
  event_base_free (ev_base);
  event_config_free (ev_cfg);
  //libevent_global_shutdown ();
}

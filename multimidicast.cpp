/* Copyright (c) 2005..2011 Dirk Jagdmann <doj@cubic.org>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you
       must not claim that you wrote the original software. If you use
       this software in a product, an acknowledgment in the product
       documentation would be appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and
       must not be misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
       distribution. */

// $Header: /code/multimidicast/multimidicast.cpp,v 1.6 2011/02/23 17:40:25 doj Exp $

#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <vector>
#include <alsa/asoundlib.h>

bool QUIET=false;

const int portnum = 1;
unsigned midi_bufsize = 250000;
const int multicast_maxsize = 1280; // maximum size of multicast packet WinXP will accept

snd_seq_t *alsa_seq=0;
int alsa_port[portnum];

void connect2MidiThroughPort(snd_seq_t *seq_handle) {
        snd_seq_addr_t sender, dest;
        snd_seq_port_subscribe_t *subs;
        int myID;
        myID=snd_seq_client_id(seq_handle);
        fprintf(stderr,"MyID=%d",myID);  
        sender.client = myID;
        sender.port = 0;
        dest.client = 14;
        dest.port = 0;
        snd_seq_port_subscribe_alloca(&subs);
        snd_seq_port_subscribe_set_sender(subs, &sender);
        snd_seq_port_subscribe_set_dest(subs, &dest);
        snd_seq_port_subscribe_set_queue(subs, 1);
        snd_seq_port_subscribe_set_time_update(subs, 1);
        snd_seq_port_subscribe_set_time_real(subs, 1);
        snd_seq_subscribe_port(seq_handle, subs);
}

void cleanup()
{
  if(alsa_seq)
    snd_seq_close(alsa_seq);
}

/**
   get interface address from supplied name.

   @param s a socket descriptor
   @param in_address (out) will contain the interface address of ifname
   @param ifname the interface name

   @return true on success, false on error

   @author Will Hall

 */
bool get_address(int s, struct in_addr *in_address, const char *ifname)
{
  struct ifreq ifr;
  strncpy (ifr.ifr_name, ifname, sizeof (ifr.ifr_name));

  if (ioctl(s, SIOCGIFFLAGS, (char*)&ifr))
    {
      perror ("ioctl:SIOCGIFFLAGS");
      return false;
    }

  if ( !(ifr.ifr_flags & IFF_UP) )
    {
      fprintf(stderr, "interface %s is down\n", ifname);
      return false;
    }

  if (ioctl(s, SIOCGIFADDR, (char*)&ifr))
    {
      perror ("ioctl:SIOCGIFADDR");
      return false;
    }

  struct sockaddr_in sa;
  memcpy (&sa, &ifr.ifr_addr, sizeof (struct sockaddr_in));
  in_address->s_addr = sa.sin_addr.s_addr;

  return true;
}

/// print help text and exit application
void help()
{
  fprintf(stderr, "options: -i <interface> - use specific network interface\n");
  fprintf(stderr, "         -h - display this text\n");
  fprintf(stderr, "         -q - quiet, don't show MIDI and network events\n");
  fprintf(stderr, "         -b <bytes> - set MIDI buffer size, default: %i bytes\n", midi_bufsize);
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
  fprintf(stderr, "multimidicast v1.3 (c) 2005..2011 by Dirk Jagdmann <doj@cubic.org>\n");

  const char *interface_name=0;

  // parse command line
  int c;
  while((c=getopt(argc, argv, "b:hi:q")) != EOF)
    switch(c)
      {
      default:
      case 'h': help();
      case 'b': midi_bufsize=strtoul(optarg, NULL, 0); break;
      case 'i': interface_name=optarg; break;
      case 'q': QUIET=true; break;
      }

  // Setup Network

  int protonum=0;
  struct protoent *p=getprotobyname("IP");
  if(p)
    protonum=p->p_proto;

  //////////////////////////////////

  int sockin[portnum];
  for(int i=0; i<portnum; ++i)
    {
      sockin[i] = socket (PF_INET, SOCK_DGRAM, protonum);
      if (sockin[i] < 0)
	{
	  perror("sockin");
	  return 1;
	}

      struct sockaddr_in sockaddr;
      memset(&sockaddr, 0, sizeof(sockaddr));
      sockaddr.sin_family=AF_INET;
      sockaddr.sin_addr.s_addr=htonl(INADDR_ANY);
      sockaddr.sin_port=htons(21928+i);

      if(bind(sockin[i], reinterpret_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr)) < 0)
	{
	  perror("bind");
	  return 1;
	}

      // Will Hall, 2007
      // INADDR_ANY will bind to default interface, specify alternate interface name
      // on command line on which to bind
      struct in_addr if_addr_in;
      if (interface_name)
	{
	  if (!get_address(sockin[i], &if_addr_in, interface_name))
	    {
	      fprintf(stderr, "could not find interface address for %s\n", interface_name);
	      return 1;
	    }
	  if (setsockopt (sockin[i], IPPROTO_IP, IP_MULTICAST_IF, &if_addr_in, sizeof(if_addr_in)))
	    {
	      perror ("setsockopt:IP_MULTICAST_IF");
	      return 1;
	    }
 	}
      else
	{
	  if_addr_in.s_addr=htonl(INADDR_ANY);
 	}

      struct ip_mreq mreq;
      mreq.imr_multiaddr.s_addr = inet_addr("225.0.0.37");
      mreq.imr_interface.s_addr = if_addr_in.s_addr;
      if(setsockopt (sockin[i], IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
	{
	  perror("setsockopt");
	  fprintf(stderr, "your linux kernel is probably missing multicast support.\n");
	  return 1;
	}
    }

  //////////////////////////////////

  int sockout[portnum];
  struct sockaddr_in addressout[portnum];
  for(int i=0; i<portnum; ++i)
    {
      sockout[i] = socket(AF_INET, SOCK_DGRAM, protonum);
      if(sockout[i] < 0)
	{
	  perror("sockout");
	  return 1;
	}

      // Will Hall, Oct 2007
      if (interface_name)
	{
	  struct in_addr if_addr_out;
	  if (!get_address(sockout[i], &if_addr_out, interface_name))
	    {
	      fprintf(stderr, "could not find interface address for %s\n", interface_name);
	      return 1;
	    }
	  if (setsockopt (sockout[i], IPPROTO_IP, IP_MULTICAST_IF, &if_addr_out, sizeof(if_addr_out)))
	    {
	      perror ("setsockopt:IP_MULTICAST_IF");
	      return 1;
	    }
	}

      memset(&addressout[i], 0, sizeof(addressout[i]));
      addressout[i].sin_family = AF_INET;
      addressout[i].sin_addr.s_addr = inet_addr("225.0.0.37");
      addressout[i].sin_port = htons(21928+i);

	// turn off loopback
      int loop = 0;
      if (setsockopt(sockout[i], IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof (loop)) < 0)
	{
	  perror ("setsockopt:IP_MULTICAST_LOOP");
	  return 1;
	}
    }

  //////////////////////////////////

  // Setup Alsa

  int alsa_err;
  if((alsa_err=snd_seq_open(&alsa_seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK)) < 0)
    {
      fprintf(stderr, "could not init ALSA sequencer: %s\n", snd_strerror(alsa_err));
      alsa_seq=0;
      return 1;
    }
  snd_seq_set_client_name(alsa_seq, argv[0]);

  for(int i=0; i<portnum; ++i)
    {
      char buf[16];
      snprintf(buf, sizeof(buf), "Port %02i", i+1);
      alsa_port[i]=snd_seq_create_simple_port(alsa_seq, buf,
					      SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE|SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
					      SND_SEQ_PORT_TYPE_APPLICATION);
      if(alsa_port[i] < 0)
	{
	  fprintf(stderr, "could not create ALSA sequencer port: %s\n", snd_strerror(alsa_port[i]));
	  return 1;
	}
    }

  // determine file descriptor of Alsa
  int alsa_fd=-1;
  {
    int npfd=snd_seq_poll_descriptors_count(alsa_seq, POLLIN);
    if(npfd<=0)
      {
	fprintf(stderr, "could not get number of ALSA sequencer poll descriptors: %s\n", snd_strerror(npfd));
	return 1;
      }

    struct pollfd *pfd=(struct pollfd*)alloca(npfd * sizeof(struct pollfd));
    if(pfd==0)
      {
	fprintf(stderr, "could not alloc memory for ALSA sequencer poll descriptors\n");
	return 1;
      }
    if(snd_seq_poll_descriptors(alsa_seq, pfd, npfd, POLLIN) != npfd)
      {
	fprintf(stderr, "number of returned poll desc is not equal of request poll desc\n");
	return 1;
      }

    alsa_fd=pfd[0].fd;
  }
  connect2MidiThroughPort(alsa_seq);
  // Setup MIDI event parsers for all ports
  snd_midi_event_t* midi_event_parser[portnum];
  for(int i=0; i<portnum; ++i)
    if((alsa_err=snd_midi_event_new(midi_bufsize, &midi_event_parser[i])) < 0)
      {
	fprintf(stderr, "could not create midi_event_parser: %s\n", snd_strerror(alsa_err));
	return 1;
      }

  ////////////////////////////////////

  while(true)
    {
      // Wait for an event
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(alsa_fd, &rfds);
      int fd_max=alsa_fd;
      for(int i=0; i<portnum; ++i)
	{
	  FD_SET(sockin[i], &rfds); if(sockin[i]>fd_max) fd_max=sockin[i];
	}

      int s=select(fd_max+1, &rfds, NULL, NULL, NULL);
      if(s < 0)
	{
	  perror("select");
	  break;
	}
      if(s==0)
	{
	  puts("timeout");
	  continue;
	}

      // A Network event
      for(int i=0; i<portnum; ++i)
	if(FD_ISSET(sockin[i], &rfds))
	  {
	    // read from network
	    unsigned char buf[1024];
	    struct sockaddr_in sender;
	    unsigned senderLen=sizeof(sender);
	    int r=recvfrom(sockin[i], buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
	    if(r>0)
	      {
		if(!QUIET)
		  {
		    fprintf(stderr, "NETW event: Port %02i %s: ", i+1, inet_ntoa(sender.sin_addr));
		    for(int j=0; j<r; ++j)
		      fprintf(stderr, "%02X ", buf[j]);
		    fprintf(stderr, "\n");
		  }
		// encode network bytes into alsa events
		unsigned char *b=buf;
		while(r>0)
		  {
		    snd_seq_event_t ev;
		    snd_seq_ev_clear(&ev);
		    snd_seq_ev_set_source(&ev, alsa_port[i]);
		    snd_seq_ev_set_subs(&ev);
		    snd_seq_ev_set_direct(&ev);
		    long rr=snd_midi_event_encode(midi_event_parser[i], b, r, &ev);
		    if(rr<0)
		      {
			fprintf(stderr, "midi_event_parser encode error: %s\n", snd_strerror(rr));
			break;
		      }
		    else if(rr==0)
		      break;

		    snd_seq_event_output(alsa_seq, &ev);
		    r-=rr;
		    b+=rr;
		  }
	      }
	    if(r<0)
	      perror("recvfrom()");
	  }

      // An Alsa Event
      if(FD_ISSET(alsa_fd, &rfds))
	do
	  {
	    // get event from Alsa
	    snd_seq_event_t *ev;
	    snd_seq_event_input(alsa_seq, &ev);
	    if(!ev) continue;

	    // ignore some events
	    switch(ev->type)
	      {
		// these are all ALSA internal events, which don't produce MIDI bytes
	      case SND_SEQ_EVENT_OSS:
	      case SND_SEQ_EVENT_CLIENT_START:
	      case SND_SEQ_EVENT_CLIENT_EXIT:
	      case SND_SEQ_EVENT_CLIENT_CHANGE:
	      case SND_SEQ_EVENT_PORT_START:
	      case SND_SEQ_EVENT_PORT_EXIT:
	      case SND_SEQ_EVENT_PORT_CHANGE:
	      case SND_SEQ_EVENT_PORT_SUBSCRIBED:
	      case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
	      case SND_SEQ_EVENT_USR0:
	      case SND_SEQ_EVENT_USR1:
	      case SND_SEQ_EVENT_USR2:
	      case SND_SEQ_EVENT_USR3:
	      case SND_SEQ_EVENT_USR4:
	      case SND_SEQ_EVENT_USR5:
	      case SND_SEQ_EVENT_USR6:
	      case SND_SEQ_EVENT_USR7:
	      case SND_SEQ_EVENT_USR8:
	      case SND_SEQ_EVENT_USR9:
	      case SND_SEQ_EVENT_BOUNCE:
	      case SND_SEQ_EVENT_USR_VAR0:
	      case SND_SEQ_EVENT_USR_VAR1:
	      case SND_SEQ_EVENT_USR_VAR2:
	      case SND_SEQ_EVENT_USR_VAR3:
	      case SND_SEQ_EVENT_USR_VAR4:
	      case SND_SEQ_EVENT_NONE:
		continue;
	      }

	    unsigned char buf_[midi_bufsize];
	    unsigned char *buf=buf_;
	    static snd_midi_event_t *dev=0;
	    if(!dev && (alsa_err=snd_midi_event_new(midi_bufsize, &dev)) < 0)
	      {
		fprintf(stderr, "could not create midi_event_parser: %s\n", snd_strerror(alsa_err));
		return 1;
	      }

	    // Decode Alsa event into raw bytes
	    long s=snd_midi_event_decode(dev, buf, midi_bufsize, ev);
	    if(s>0)
	      {
		// Send bytes to network
		int p=ev->dest.port;
		if(!QUIET)
		  {
		    fprintf(stderr, "MIDI event: Port %02i: ", p);
		    for(int j=0; j<s; ++j)
		      fprintf(stderr, "%02X ", buf[j]);
		    fprintf(stderr, "\n");
		  }
		// split multicast datagrams into multicast_maxsize
		while(s)
		  {
		    int ss=(s>multicast_maxsize)?multicast_maxsize:s;
		    if(sendto(sockout[p], buf, ss, 0, reinterpret_cast<struct sockaddr*>(&addressout[p]), sizeof(addressout[p])) < 0)
		      perror("sendto()");
		    s-=ss;
		    buf+=ss;
		  }
	      }
	    else if(s<0) {
	      fprintf(stderr, "could not decode midi event: %li, %s, midi_bufsize=%u\n", s, snd_strerror(s), midi_bufsize);
	      if(s == -ENOMEM) {
		snd_midi_event_free(dev);
		dev = 0;
		midi_bufsize *= 2;
		fprintf(stderr, "increased midi_bufsize to %u bytes...\n", midi_bufsize);
	      }
	    }

	    if(dev)
	      snd_midi_event_reset_decode(dev);
	  }
	while (snd_seq_event_input_pending(alsa_seq, 0) > 0);

      snd_seq_drain_output(alsa_seq);
    }

  return 0;
}

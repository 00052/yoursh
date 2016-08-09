#include <sys/socket.h>
#include <netdb.h>
#include <termcap.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <paths.h>
#include "telnetd.h"
//#include "pathnames.h"
//#include "setproctitle.h"
/*
*/
/* In Linux, this is an enum */
#if defined(__linux__) || defined(IPPROTO_IP)
#define HAS_IPPROTO_IP
#endif

#define DEFAULT_LISTEN "23"

static void doit(struct sockaddr *who, socklen_t who_len);
static int terminaltypeok(const char *s);

/*
 * I/O data buffers,
 * pointers, and counters.
 */
char	ptyibuf[BUFSIZ], *ptyip = ptyibuf;
char	ptyibuf2[BUFSIZ];


int debug = 0;
int keepalive = 1;
char *loginprg = LOGIN_WRAPPER;

static int got_sigchld = 0;

extern void usage(void);

static void
catch_sigchld(int sig)
{
	got_sigchld = 1;
}

void get_slc_defaults(void) {
    int i;
    init_termbuf();
    for (i = 1; i <= NSLC; i++) {
        slctab[i].defset.flag = spcset(i, &slctab[i].defset.val, 
                                       &slctab[i].sptr);
        slctab[i].current.flag = SLC_NOSUPPORT; 
        slctab[i].current.val = 0;  
    }   
}

static void
wait_for_connection(const char *service)
{
	struct addrinfo hints;
	struct addrinfo *res, *addr;
	struct pollfd *fds, *fdp;
	int nfds;
	int i;
	int error;
	int on = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(NULL, service, &hints, &res);
	if (error) {
		char *p;
		error = asprintf(&p, "getaddrinfo: %s\n", gai_strerror(error));
		fatal(2, error >= 0 ? p : "");
	}

	for (addr = res, nfds = 0; addr; addr = addr->ai_next, nfds++)
		;
	fds = malloc(sizeof(struct pollfd) * nfds);
	for (addr = res, fdp = fds; addr; addr = addr->ai_next, fdp++) {
		int s;

		if (addr->ai_family == AF_LOCAL) {
nextaddr:
			fdp--;
			nfds--;
			continue;
		}

		s = socket(addr->ai_family, SOCK_STREAM, 0);
		if (s < 0) {
			if (errno == EAFNOSUPPORT || errno == EINVAL) {
				goto nextaddr;
			}
			fatalperror(2, "socket");
		}
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
			fatalperror(2, "setsockopt");
		}
		if (bind(s, addr->ai_addr, addr->ai_addrlen)) {
#ifdef linux
			if (fdp != fds && errno == EADDRINUSE) {
				close(s);
				goto nextaddr;
			}
#endif
			fatalperror(2, "bind");
		}
		if (listen(s, 1)) {
			fatalperror(2, "listen");
		}
		if (fcntl(s, F_SETFL, O_NONBLOCK)) {
			fatalperror(2, "fcntl");
		}

		fdp->fd = s;
		fdp->events = POLLIN;
	}

	freeaddrinfo(res);

	while (1) {
		if (poll(fds, nfds, -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			fatalperror(2, "poll");
		}

		for (i = 0, fdp = fds; i < nfds; i++, fdp++) {
			int fd;

			if (!(fdp->revents & POLLIN)) {
				continue;
			}

			fd = accept(fdp->fd, 0, 0);
			if (fd >= 0) {
				dup2(fd, 0);
				close(fd);
				goto out;
			}
			if (errno != EAGAIN) {
				fatalperror(2, "accept");
			}
		}
	}

out:
	for (i = 0, fdp = fds; i < nfds; i++, fdp++) {
		close(fdp->fd);
	}
	free(fds);
}

static void 
reverse_connect(char *address) {
	struct sockaddr_in client_addr;
	char * hostname;
	unsigned short port;
	int i;
	for(i=0; address[i] && address[i] != ':'; i++) 
		;
	if(address[i] == '\0' || address[i+1] == '\0') {
		fatal(2,"invalid address for r");
	}
	port = atoi(address + i + 1);
	if(port == 0) {
		fatal(2,"invalid port value for r");
	}
	address[i] = '\0';
	hostname = address;
	bzero(&client_addr,sizeof(client_addr)); 
	client_addr.sin_family = AF_INET;    
	client_addr.sin_addr.s_addr = htons(INADDR_ANY);
	client_addr.sin_port = htons(0);    
	int client_socket = socket(AF_INET,SOCK_STREAM,0);

	if( client_socket < 0)
		fatalperror(2,"socket");
	if( bind(client_socket,(struct sockaddr*)&client_addr,sizeof(client_addr)))
		fatalperror(2,"bind");

	struct sockaddr_in server_addr;
	bzero(&server_addr,sizeof(server_addr));
	server_addr.sin_family = AF_INET;

	if(inet_aton("127.0.0.1",&server_addr.sin_addr) == 0) 
		fatalperror(2,"inet_aton");
	server_addr.sin_port = htons(10000);
	socklen_t server_addr_length = sizeof(server_addr);

	if(connect(client_socket,(struct sockaddr*)&server_addr, server_addr_length) < 0)
		fatalperror(2,"connect");
	dup2(client_socket,0);
	close(client_socket);
	return 0;
	
}

int
main(int argc, char *argv[], char *env[])
{
	struct sockaddr_storage from;
	int on = 1;
	socklen_t fromlen;
	register int ch;
	int listening = 0;
	char* listen_port = DEFAULT_LISTEN;
	char* reverse_addr = NULL;
#if	defined(HAS_IPPROTO_IP) && defined(IP_TOS)
	int tos = -1;
#endif

	//initsetproctitle(argc, argv, env);

	pfrontp = pbackp = ptyobuf;
	netip = netibuf;

	while ((ch = getopt(argc, argv, "r:l:L:n:h")) != EOF) {
		switch(ch) {
		case 'l':
			listening = 1;
			listen_port = strdup(optarg);
			break;
		case 'r':
			reverse_addr = strdup(optarg);
			break;
		case 'L':
			loginprg = strdup(optarg);
			break;
		case 'n':
			keepalive = 0;
			break;
		case 'h':
			usage();
			exit(0);
		default:
			fprintf(stderr, "telnetd: %c: unknown option\n", ch);
		case '?':
			usage();
			exit(1);
		}
	}

	if(listening) {
		if(reverse_addr) {  /* -l -r*/
			fprintf(stderr,"telnetd: unexpect option -r\n");
			exit(1);
		} 
		wait_for_connection(listen_port); /* -l */
	} else {
		if(reverse_addr) { /* -r */
			printf("Reverse connect: %s\n",reverse_addr);
			reverse_connect(reverse_addr);
		} else
			wait_for_connection(listen_port); /* neither -l -r */
	}


	fromlen = sizeof (from);
	if (getpeername(0, (struct sockaddr *)&from, &fromlen) < 0) {
		fatalperror(2, "getpeername");
	}
	if (keepalive &&
	    setsockopt(0, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof (on)) < 0) {
	}
#if	defined(HAS_IPPROTO_IP) && defined(IP_TOS)
	
	{
		if (tos < 0)
			tos = 020;	/* Low Delay bit */
		if (tos
		   && (setsockopt(0, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0)
		   && (errno != ENOPROTOOPT) )
			fprintf(stderr,"setsockopt (IP_TOS): %d",errno);
	}
#endif
	net = 0;
	netopen();
	doit((struct sockaddr *)&from, fromlen);
	/* NOTREACHED */
	return 0;
}  /* end of main */

void
usage(void)
{
	fprintf(stderr, "Usage: telnetd [-l port | -r host:port] [-L program] [-n] [-h]\n");
}

/*
 * getterminaltype
 *
 *	Ask the other end to send along its terminal type and speed.
 * Output is the variable terminaltype filled in.
 */

static void _gettermname(void);

static
int
getterminaltype(char *name)
{
    int retval = -1;
    (void)name;

    settimer(baseline);

    send_do(TELOPT_TTYPE, 1);
    send_do(TELOPT_TSPEED, 1);
    send_do(TELOPT_XDISPLOC, 1);
    send_do(TELOPT_ENVIRON, 1);
    while (
	   his_will_wont_is_changing(TELOPT_TTYPE) ||
	   his_will_wont_is_changing(TELOPT_TSPEED) ||
	   his_will_wont_is_changing(TELOPT_XDISPLOC) ||
	   his_will_wont_is_changing(TELOPT_ENVIRON)) {
	ttloop();
    }
    if (his_state_is_will(TELOPT_TSPEED)) {
	netoprintf("%c%c%c%c%c%c", 
		   IAC, SB, TELOPT_TSPEED, TELQUAL_SEND, IAC, SE);
    }
    if (his_state_is_will(TELOPT_XDISPLOC)) {
	netoprintf("%c%c%c%c%c%c", 
		   IAC, SB, TELOPT_XDISPLOC, TELQUAL_SEND, IAC, SE);
    }
    if (his_state_is_will(TELOPT_ENVIRON)) {
	netoprintf("%c%c%c%c%c%c", 
		   IAC, SB, TELOPT_ENVIRON, TELQUAL_SEND, IAC, SE);
    }
    if (his_state_is_will(TELOPT_TTYPE)) {
       netoprintf("%c%c%c%c%c%c", 
		  IAC, SB, TELOPT_TTYPE, TELQUAL_SEND, IAC, SE);
    }
    if (his_state_is_will(TELOPT_TSPEED)) {
	while (sequenceIs(tspeedsubopt, baseline))
	    ttloop();
    }
    if (his_state_is_will(TELOPT_XDISPLOC)) {
	while (sequenceIs(xdisplocsubopt, baseline))
	    ttloop();
    }
    if (his_state_is_will(TELOPT_ENVIRON)) {
	while (sequenceIs(environsubopt, baseline))
	    ttloop();
    }
    if (his_state_is_will(TELOPT_TTYPE)) {
	char first[256], last[256];

	while (sequenceIs(ttypesubopt, baseline))
	    ttloop();

	/*
	 * If the other side has already disabled the option, then
	 * we have to just go with what we (might) have already gotten.
	 */
	if (his_state_is_will(TELOPT_TTYPE) && !terminaltypeok(terminaltype)) {
	    /*
	     * Due to state.c, terminaltype points to a static char[41].
	     * Therefore, this assert cannot fail, and therefore, strings
	     * arising from "terminaltype" can be safely strcpy'd into
	     * first[] or last[].
	     */
	    assert(strlen(terminaltype) < sizeof(first));

	    strcpy(first, terminaltype);

	    for(;;) {
		/*
		 * Save the unknown name, and request the next name.
		 */
		strcpy(last, terminaltype);

		_gettermname();
		assert(strlen(terminaltype) < sizeof(first));

		if (terminaltypeok(terminaltype))
		    break;

		if (!strcmp(last, terminaltype) ||
		    his_state_is_wont(TELOPT_TTYPE)) {
		    /*
		     * We've hit the end.  If this is the same as
		     * the first name, just go with it.
		     */
		    if (!strcmp(first, terminaltype))
			break;
		    /*
		     * Get the terminal name one more time, so that
		     * RFC1091 compliant telnets will cycle back to
		     * the start of the list.
		     */
		     _gettermname();
		    assert(strlen(terminaltype) < sizeof(first));

		    if (strcmp(first, terminaltype)) {
			/*
			 * first[] came from terminaltype, so it must fit
			 * back in.
			 */
			strcpy(terminaltype, first);
		    }
		    break;
		}
	    }
	}
    }
    return(retval);
}  /* end of getterminaltype */

static
void
_gettermname(void)
{
    /*
     * If the client turned off the option,
     * we can't send another request, so we
     * just return.
     */
    if (his_state_is_wont(TELOPT_TTYPE))
	return;

    settimer(baseline);
    netoprintf("%c%c%c%c%c%c", IAC, SB, TELOPT_TTYPE, TELQUAL_SEND, IAC, SE);
    while (sequenceIs(ttypesubopt, baseline))
	ttloop();
}

static int
terminaltypeok(const char *s)
{
    /* char buf[2048]; */

    if (terminaltype == NULL)
	return(1);

    /*
     * Fix from Chris Evans: if it has a / in it, termcap will
     * treat it as a filename. Oops.
     */
    if (strchr(s, '/')) {
	return 0;
    }

    /*
     * If it's absurdly long, accept it without asking termcap.
     *
     * This means that it won't get seen again until after login,
     * at which point exploiting buffer problems in termcap doesn't
     * gain one anything.
     *
     * It's possible this limit ought to be raised to 128, but nothing
     * in my termcap is more than 64, 64 is _plenty_ for most, and while
     * buffers aren't likely to be smaller than 64, they might be 80 and
     * thus less than 128.
     */
    if (strlen(s) > 63) {
       return 0;
    }

    /*
     * tgetent() will return 1 if the type is known, and
     * 0 if it is not known.  If it returns -1, it couldn't
     * open the database.  But if we can't open the database,
     * it won't help to say we failed, because we won't be
     * able to verify anything else.  So, we treat -1 like 1.
     */

    /*
     * Don't do this - tgetent is not really trustworthy. Assume
     * the terminal type is one we know; terminal types are pretty
     * standard now. And if it isn't, it's unlikely we're going to
     * know anything else the remote telnet might send as an alias
     * for it.
     *
     * if (tgetent(buf, s) == 0)
     *    return(0);
     */
    return(1);
}

#ifndef	MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN 64
#endif	/* MAXHOSTNAMELEN */

char host_name[MAXHOSTNAMELEN];
char remote_host_name[MAXHOSTNAMELEN];

extern void telnet(int, int);

/*
 * Get a pty, scan input lines.
 */
static void
doit(struct sockaddr *who, socklen_t who_len)
{
	const char *host;
	int level;
	char user_name[256];
	int i;
	struct addrinfo hints, *res;

	/*
	 * Find an available pty to use.
	 */
	pty = getpty();
	if (pty < 0)
		fatalperror(net, "getpty");

	/* get name of connected client */
	if (getnameinfo(who, who_len, remote_host_name,
			sizeof(remote_host_name), 0, 0, 0)) {
		fprintf(stderr, "doit: getnameinfo: %d\n",errno);
		*remote_host_name = 0;
        }

	/* Disallow funnies. */
	for (i=0; remote_host_name[i]; i++) {
	    if (remote_host_name[i]<=32 || remote_host_name[i]>126) 
		remote_host_name[i] = '?';
	}
	host = remote_host_name;

	/* Get local host name */
	gethostname(host_name, sizeof(host_name));
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_CANONNAME;
	if ((i = getaddrinfo(host_name, 0, &hints, &res)))
		fprintf(stderr, "doit: getaddrinfo: %s\n", gai_strerror(i));
	else {
		strncpy(host_name, res->ai_canonname, sizeof(host_name)-1);
		host_name[sizeof(host_name)-1] = 0;
	}

	//init_env();
	/*
	 * get terminal type.
	 */
	*user_name = 0;
	level = getterminaltype(user_name);
	setenv("TERM", terminaltype ? terminaltype : "network", 1);

	/* TODO list stuff provided by Laszlo Vecsey <master@internexus.net> */

	/*
	 * Set REMOTEHOST environment variable
	 */
	//setproctitle("%s", host);
	setenv("REMOTEHOST", host, 0);

	/*
	 * Start up the login process on the slave side of the terminal
	 */
	startslave(host, level, user_name);

	telnet(net, pty);  /* begin server processing */

	/*NOTREACHED*/
}  /* end of doit */

/*
 * Main loop.  Select from pty and network, and
 * hand data to telnet receiver finite state machine.
 */
void telnet(int f, int p)
{
    int on = 1;
    char *HE;

    /*
     * Initialize the slc mapping table.
     */
    get_slc_defaults();

    /*
     * Do some tests where it is desireable to wait for a response.
     * Rather than doing them slowly, one at a time, do them all
     * at once.
     */
    if (my_state_is_wont(TELOPT_SGA))
	send_will(TELOPT_SGA, 1);
    /*
     * Is the client side a 4.2 (NOT 4.3) system?  We need to know this
     * because 4.2 clients are unable to deal with TCP urgent data.
     *
     * To find out, we send out a "DO ECHO".  If the remote system
     * answers "WILL ECHO" it is probably a 4.2 client, and we note
     * that fact ("WILL ECHO" ==> that the client will echo what
     * WE, the server, sends it; it does NOT mean that the client will
     * echo the terminal input).
     */
    send_do(TELOPT_ECHO, 1);
    

    /*
     * Send along a couple of other options that we wish to negotiate.
     */
    send_do(TELOPT_NAWS, 1);
    send_will(TELOPT_STATUS, 1);
    flowmode = 1;  /* default flow control state */
    send_do(TELOPT_LFLOW, 1);
    
    /*
     * Spin, waiting for a response from the DO ECHO.  However,
     * some REALLY DUMB telnets out there might not respond
     * to the DO ECHO.  So, we spin looking for NAWS, (most dumb
     * telnets so far seem to respond with WONT for a DO that
     * they don't understand...) because by the time we get the
     * response, it will already have processed the DO ECHO.
     * Kludge upon kludge.
     */
    while (his_will_wont_is_changing(TELOPT_NAWS)) {
	ttloop();
    }
    
    /*
     * But...
     * The client might have sent a WILL NAWS as part of its
     * startup code; if so, we'll be here before we get the
     * response to the DO ECHO.  We'll make the assumption
     * that any implementation that understands about NAWS
     * is a modern enough implementation that it will respond
     * to our DO ECHO request; hence we'll do another spin
     * waiting for the ECHO option to settle down, which is
     * what we wanted to do in the first place...
     */
    if (his_want_state_is_will(TELOPT_ECHO) &&
	his_state_is_will(TELOPT_NAWS)) {
	while (his_will_wont_is_changing(TELOPT_ECHO))
	    ttloop();
    }
    /*
     * On the off chance that the telnet client is broken and does not
     * respond to the DO ECHO we sent, (after all, we did send the
     * DO NAWS negotiation after the DO ECHO, and we won't get here
     * until a response to the DO NAWS comes back) simulate the
     * receipt of a will echo.  This will also send a WONT ECHO
     * to the client, since we assume that the client failed to
     * respond because it believes that it is already in DO ECHO
     * mode, which we do not want.
     */
    if (his_want_state_is_will(TELOPT_ECHO)) {
	willoption(TELOPT_ECHO);
    }
    
    /*
     * Finally, to clean things up, we turn on our echo.  This
     * will break stupid 4.2 telnets out of local terminal echo.
     */
    
    if (my_state_is_wont(TELOPT_ECHO))
	send_will(TELOPT_ECHO, 1);
    
    /*
     * Turn on packet mode
     */
    ioctl(p, TIOCPKT, (char *)&on);
    
    /*
     * Call telrcv() once to pick up anything received during
     * terminal type negotiation, 4.2/4.3 determination, and
     * linemode negotiation.
     */
    telrcv();
    
    ioctl(f, FIONBIO, (char *)&on);
    ioctl(p, FIONBIO, (char *)&on);

#if defined(SO_OOBINLINE)
    setsockopt(net, SOL_SOCKET, SO_OOBINLINE, &on, sizeof on);
#endif	/* defined(SO_OOBINLINE) */
    
#ifdef	SIGTSTP
    signal(SIGTSTP, SIG_IGN);
#endif
#ifdef	SIGTTOU
    /*
     * Ignoring SIGTTOU keeps the kernel from blocking us
     * in ttioct() in /sys/tty.c.
     */
    signal(SIGTTOU, SIG_IGN);
#endif

    signal(SIGCHLD, catch_sigchld);

#ifdef TIOCNOTTY
    {
	register int t;
	t = open(_PATH_TTY, O_RDWR);
	if (t >= 0) {
	    (void) ioctl(t, TIOCNOTTY, (char *)0);
	    (void) close(t);
	}
    }
#endif
    
    /*
     * Show banner that getty never gave.
     *
     * We put the banner in the pty input buffer.  This way, it
     * gets carriage return null processing, etc., just like all
     * other pty --> client data.
     */
    
    HE = 0;

    edithost(HE, host_name);
    
    if (pcc) strncat(ptyibuf2, ptyip, pcc+1);
    ptyip = ptyibuf2;
    pcc = strlen(ptyip);

    
    for (;;) {
	fd_set ibits, obits, xbits;
	int c, hifd;
	
	if (ncc < 0 && pcc < 0)
	    break;
	
	FD_ZERO(&ibits);
	FD_ZERO(&obits);
	FD_ZERO(&xbits);
	hifd=0;
	/*
	 * Never look for input if there's still
	 * stuff in the corresponding output buffer
	 */
	if (netbuflen(1) || pcc > 0) {
	    FD_SET(f, &obits);
	    if (f >= hifd) hifd = f+1;
	} 
	else {
	    FD_SET(p, &ibits);
	    if (p >= hifd) hifd = p+1;
	}
	if (pfrontp - pbackp || ncc > 0) {
	    FD_SET(p, &obits);
	    if (p >= hifd) hifd = p+1;
	} 
	else {
	    FD_SET(f, &ibits);
	    if (f >= hifd) hifd = f+1;
	}
	if (!SYNCHing) {
	    FD_SET(f, &xbits);
	    if (f >= hifd) hifd = f+1;
	}
	if ((c = select(hifd, &ibits, &obits, &xbits,
			(struct timeval *)0)) < 1) {
	    if (c == -1) {
		if (errno == EINTR) {
		    continue;
		}
	    }
	    sleep(5);
	    continue;
	}
	
	/*
	 * Any urgent data?
	 */
	if (FD_ISSET(net, &xbits)) {
	    SYNCHing = 1;
	}
	
	/*
	 * Something to read from the network...
	 */
	if (FD_ISSET(net, &ibits)) {
#if !defined(SO_OOBINLINE)
	    /*
	     * In 4.2 (and 4.3 beta) systems, the
	     * OOB indication and data handling in the kernel
	     * is such that if two separate TCP Urgent requests
	     * come in, one byte of TCP data will be overlaid.
	     * This is fatal for Telnet, but we try to live
	     * with it.
	     *
	     * In addition, in 4.2 (and...), a special protocol
	     * is needed to pick up the TCP Urgent data in
	     * the correct sequence.
	     *
	     * What we do is:  if we think we are in urgent
	     * mode, we look to see if we are "at the mark".
	     * If we are, we do an OOB receive.  If we run
	     * this twice, we will do the OOB receive twice,
	     * but the second will fail, since the second
	     * time we were "at the mark", but there wasn't
	     * any data there (the kernel doesn't reset
	     * "at the mark" until we do a normal read).
	     * Once we've read the OOB data, we go ahead
	     * and do normal reads.
	     *
	     * There is also another problem, which is that
	     * since the OOB byte we read doesn't put us
	     * out of OOB state, and since that byte is most
	     * likely the TELNET DM (data mark), we would
	     * stay in the TELNET SYNCH (SYNCHing) state.
	     * So, clocks to the rescue.  If we've "just"
	     * received a DM, then we test for the
	     * presence of OOB data when the receive OOB
	     * fails (and AFTER we did the normal mode read
	     * to clear "at the mark").
	     */
	    if (SYNCHing) {
		int atmark;
		
		ioctl(net, SIOCATMARK, (char *)&atmark);
		if (atmark) {
		    ncc = recv(net, netibuf, sizeof (netibuf), MSG_OOB);
		    if ((ncc == -1) && (errno == EINVAL)) {
			ncc = read(net, netibuf, sizeof (netibuf));
			if (sequenceIs(didnetreceive, gotDM)) {
			    SYNCHing = stilloob(net);
			}
		    }
		} 
		else {
		    ncc = read(net, netibuf, sizeof (netibuf));
		}
	    } 
	    else {
		ncc = read(net, netibuf, sizeof (netibuf));
	    }
	    settimer(didnetreceive);
#else	/* !defined(SO_OOBINLINE)) */
	    ncc = read(net, netibuf, sizeof (netibuf));
#endif	/* !defined(SO_OOBINLINE)) */
	    if (ncc < 0 && errno == EWOULDBLOCK)
		ncc = 0;
	    else {
		if (ncc <= 0) {
		    break;
		}
		netip = netibuf;
	    }
	}
	
	/*
	 * Something to read from the pty...
	 */
	if (FD_ISSET(p, &ibits)) {
	    pcc = read(p, ptyibuf, BUFSIZ);
	    /*
	     * On some systems, if we try to read something
	     * off the master side before the slave side is
	     * opened, we get EIO.
	     */
	    if (pcc < 0 && (errno == EWOULDBLOCK || errno == EIO)) {
		pcc = 0;
	    } 
	    else {
		if (pcc <= 0)
		    break;
		if (ptyibuf[0] & TIOCPKT_FLUSHWRITE) {
		    static const char msg[] = { IAC, DM };
		    netclear();	/* clear buffer back */
#ifndef	NO_URGENT
		    /*
		     * There are client telnets on some
		     * operating systems get screwed up
		     * royally if we send them urgent
		     * mode data.
		     */
		    sendurg(msg, sizeof(msg));
#endif
		}
		if (his_state_is_will(TELOPT_LFLOW) &&
		    (ptyibuf[0] &
		     (TIOCPKT_NOSTOP|TIOCPKT_DOSTOP))) {
			netoprintf("%c%c%c%c%c%c",
				   IAC, SB, TELOPT_LFLOW,
				   ptyibuf[0] & TIOCPKT_DOSTOP ? 1 : 0,
				   IAC, SE);
		}


		pcc--;
		ptyip = ptyibuf+1;
	    }
	}
	
	while (pcc > 0 && !netbuflen(0)) {
	    c = *ptyip++ & 0377, pcc--;
	    if (c == IAC)
		putc(c, netfile);
	    putc(c, netfile);
	    if ((c == '\r'  ) && (my_state_is_wont(TELOPT_BINARY))) {
		if (pcc > 0 && ((*ptyip & 0377) == '\n')) {
		    putc(*ptyip++ & 0377, netfile);
		    pcc--;
		} 
		else putc('\0', netfile);
	    }
	}

	if (FD_ISSET(f, &obits))
	    netflush();
	if (ncc > 0)
	    telrcv();
	if (FD_ISSET(p, &obits) && (pfrontp - pbackp) > 0)
	    ptyflush();

    	if (got_sigchld) {
	    netflush();
	    cleanup(SIGCHLD);	/* Not returning.  */
	}
    }
    cleanup(0);
}  /* end of telnet */
	
#ifndef	TCSIG
# ifdef	TIOCSIG
#  define TCSIG TIOCSIG
# endif
#endif

/*
 * Send interrupt to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write intr char.
 */
void interrupt(void) {
    ptyflush();	/* half-hearted */
    
#ifdef	TCSIG
    (void) ioctl(pty, TCSIG, (char *)SIGINT);
#else	/* TCSIG */
    init_termbuf();
    *pfrontp++ = slctab[SLC_IP].sptr ?
	 (unsigned char)*slctab[SLC_IP].sptr : '\177';
#endif	/* TCSIG */
}

/*
 * Send quit to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write quit char.
 */
void sendbrk(void) {
    ptyflush();	/* half-hearted */
#ifdef	TCSIG
    (void) ioctl(pty, TCSIG, (char *)SIGQUIT);
#else	/* TCSIG */
    init_termbuf();
    *pfrontp++ = slctab[SLC_ABORT].sptr ?
	 (unsigned char)*slctab[SLC_ABORT].sptr : '\034';
#endif	/* TCSIG */
}

void sendsusp(void) {
#ifdef	SIGTSTP
    ptyflush();	/* half-hearted */
# ifdef	TCSIG
    (void) ioctl(pty, TCSIG, (char *)SIGTSTP);
# else	/* TCSIG */
    *pfrontp++ = slctab[SLC_SUSP].sptr ?
	(unsigned char)*slctab[SLC_SUSP].sptr : '\032';
# endif	/* TCSIG */
#endif	/* SIGTSTP */
}

/*
 * When we get an AYT, if ^T is enabled, use that.  Otherwise,
 * just send back "[Yes]".
 */
void recv_ayt(void) {
#if	defined(SIGINFO) && defined(TCSIG)
    if (slctab[SLC_AYT].sptr && *slctab[SLC_AYT].sptr != _POSIX_VDISABLE) {
	(void) ioctl(pty, TCSIG, (char *)SIGINFO);
	return;
    }
#endif
    netoprintf("\r\n[%s : yes]\r\n", host_name);
}

void doeof(void) {
    init_termbuf();

#if	 (VEOF == VMIN)
    if (!tty_isediting()) {
	extern char oldeofc;
	*pfrontp++ = oldeofc;
	return;
    }
#endif
    *pfrontp++ = slctab[SLC_EOF].sptr ?
	     (unsigned char)*slctab[SLC_EOF].sptr : '\004';
}

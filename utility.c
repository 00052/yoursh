/*
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * From: @(#)utility.c	5.8 (Berkeley) 3/22/91
 */
char util_rcsid[] = 
  "$Id: utility.c,v 1.11 1999/12/12 14:59:45 dholland Exp $";
#define PRINTOPTIONS

#include <stdarg.h>
#include <sys/utsname.h>
#include <sys/time.h>


#include "telnetd.h"

struct buflist {
	struct buflist *next;
	char *buf;
	size_t len;
};

static struct buflist head = { next: &head, buf: 0, len: 0 };
static struct buflist *tail = &head;
static size_t skip;
static int trailing;
static size_t listlen;
static int doclear;
static struct buflist *urg;

/*
 * ttloop
 *
 *	A small subroutine to flush the network output buffer, get some data
 * from the network, and pass it through the telnet state machine.  We
 * also flush the pty input buffer (by dropping its data) if it becomes
 * too full.
 */

void
ttloop(void)
{

		     
    netflush();
    ncc = read(net, netibuf, sizeof(netibuf));
    if (ncc < 0) {
	syslog(LOG_INFO, "ttloop: read: %m\n");
	exit(1);
    } else if (ncc == 0) {
	syslog(LOG_INFO, "ttloop: peer died: EOF\n");
	exit(1);
    }
    netip = netibuf;
    telrcv();			/* state machine */
    if (ncc > 0) {
	pfrontp = pbackp = ptyobuf;
	telrcv();
    }
}  /* end of ttloop */

/*
 * Check a descriptor to see if out of band data exists on it.
 */
int stilloob(int s)		/* socket number */
{
    static struct timeval timeout = { 0, 0 };
    fd_set	excepts;
    int value;

    do {
	FD_ZERO(&excepts);
	FD_SET(s, &excepts);
	value = select(s+1, (fd_set *)0, (fd_set *)0, &excepts, &timeout);
    } while ((value == -1) && (errno == EINTR));

    if (value < 0) {
	fatalperror(pty, "select");
    }
    if (FD_ISSET(s, &excepts)) {
	return 1;
    } else {
	return 0;
    }
}

void 	ptyflush(void)
{
	int n;

	if ((n = pfrontp - pbackp) > 0) {
		n = write(pty, pbackp, n);
	}
	if (n < 0) {
		if (errno == EWOULDBLOCK || errno == EINTR)
			return;
		cleanup(0);
	}
	pbackp += n;
	if (pbackp == pfrontp)
		pbackp = pfrontp = ptyobuf;
}

/*
 * nextitem()
 *
 *	Return the address of the next "item" in the TELNET data
 * stream.  This will be the address of the next character if
 * the current address is a user data character, or it will
 * be the address of the character following the TELNET command
 * if the current address is a TELNET IAC ("I Am a Command")
 * character.
 */
static
const char *
nextitem(
	const unsigned char *current, const unsigned char *end,
	const unsigned char *next, const unsigned char *nextend
) {
	if (*current++ != IAC) {
		while (current < end && *current++ != IAC)
			;
		goto out;
	}

	if (current >= end) {
		current = next;
		if (!current) {
			return 0;
		}
		end = nextend;
		next = 0;
	}

	switch (*current++) {
	case DO:
	case DONT:
	case WILL:
	case WONT:
		current++;
		break;
	case SB:		/* loop forever looking for the SE */
		for (;;) {
			int iac;

			while (iac = 0, current < end) {
				if (*current++ == IAC) {
					if (current >= end) {
						iac = 1;
						break;
					}
iac:
					if (*current++ == SE) {
						goto out;
					}
				}
			}

			current = next;
			if (!current) {
				return 0;
			}
			end = nextend;
			next = 0;
			if (iac) {
				goto iac;
			}
		}
	}

out:
	return next ? next + (current - end) : current;
}  /* end of nextitem */


/*
 * netclear()
 *
 *	We are about to do a TELNET SYNCH operation.  Clear
 * the path to the network.
 *
 *	Things are a bit tricky since we may have sent the first
 * byte or so of a previous TELNET command into the network.
 * So, we have to scan the network buffer from the beginning
 * until we are up to where we want to be.
 *
 *	A side effect of what we do, just to keep things
 * simple, is to clear the urgent data pointer.  The principal
 * caller should be setting the urgent data pointer AFTER calling
 * us in any case.
 */
void netclear(void)
{
	doclear++;
	netflush();
	doclear--;
}  /* end of netclear */

static void
netwritebuf(void)
{
	struct iovec *vector;
	struct iovec *v;
	struct buflist *lp;
	ssize_t n;
	size_t len;
	int ltrailing = trailing;

	if (!listlen)
		return;

	vector = malloc(listlen * sizeof(struct iovec));
	if (!vector) {
		return;
	}

	len = listlen - (doclear & ltrailing);
	v = vector;
	lp = head.next;
	while (lp != &head) {
		if (lp == urg) {
			len = v - vector;
			if (!len) {
				n = send(net, lp->buf, 1, MSG_OOB);
				if (n > 0) {
					urg = 0;
				}
				goto epi;
			}
			break;
		}
		v->iov_base = lp->buf;
		v->iov_len = lp->len;
		v++;
		lp = lp->next;
	}

	vector->iov_base = (char *)vector->iov_base + skip;
	vector->iov_len -= skip;

	n = writev(net, vector, len);

epi:
	free(vector);

	if (n < 0) {
		if (errno != EWOULDBLOCK && errno != EINTR)
			cleanup(0);
		return;
	}

	len = n + skip;

	lp = head.next;
	while (lp->len <= len) {
		if (lp == tail && ltrailing) {
			break;
		}

		len -= lp->len;

		head.next = lp->next;
		listlen--;
		free(lp->buf);
		free(lp);

		lp = head.next;
		if (lp == &head) {
			tail = &head;
			break;
		}
	}

	skip = len;
}

/*
 *  netflush
 *             Send as much data as possible to the network,
 *     handling requests for urgent data.
 */
void
netflush(void)
{
	if (fflush(netfile)) {
		/* out of memory? */
		cleanup(0);
	}
	netwritebuf();
}


/*
 * miscellaneous functions doing a variety of little jobs follow ...
 */


void
fatal(int f, const char *msg)
{
	char buf[BUFSIZ];

	(void) snprintf(buf, sizeof(buf), "telnetd: %s.\r\n", msg);
	(void) write(f, buf, (int)strlen(buf));
	sleep(1);	/*XXX*/
	exit(1);
}

void
fatalperror(int f, const char *msg)
{
	char buf[BUFSIZ];
	snprintf(buf, sizeof(buf), "%s: %s\r\n", msg, strerror(errno));
	fatal(f, buf);
}

char *editedhost;
struct utsname kerninfo;

void
edithost(const char *pat, const char *host)
{
	char *res;

	uname(&kerninfo);

	if (!pat)
		pat = "";

	res = realloc(editedhost, strlen(pat) + strlen(host) + 1);
	if (!res) {
		if (editedhost) {
			free(editedhost);
			editedhost = 0;
		}
		fprintf(stderr, "edithost: Out of memory\n");
		return;
	}
	editedhost = res;

	while (*pat) {
		switch (*pat) {

		case '#':
			if (*host)
				host++;
			break;

		case '@':
			if (*host)
				*res++ = *host++;
			break;

		default:
			*res++ = *pat;
			break;
		}
		pat++;
	}
	if (*host)
		(void) strcpy(res, host);
	else
		*res = '\0';
}

static char *putlocation;

static 
void
putstr(const char *s)
{
    while (*s) putchr(*s++);
}

void putchr(int cc)
{
	*putlocation++ = cc;
}

static char fmtstr[] = { "%H:%M on %A, %d %B %Y" };

void putf(const char *cp, char *where)
{
	char *slash;
	time_t t;
	char db[100];

	if (where)
	putlocation = where;

	while (*cp) {
		if (*cp != '%') {
			putchr(*cp++);
			continue;
		}
		switch (*++cp) {

		case 't':
			slash = strrchr(line, '/');
			if (slash == NULL)
				putstr(line);
			else
				putstr(slash+1);
			break;

		case 'h':
			if (editedhost) {
				putstr(editedhost);
			}
			break;

		case 'd':
			(void)time(&t);
			(void)strftime(db, sizeof(db), fmtstr, localtime(&t));
			putstr(db);
			break;

		case '%':
			putchr('%');
			break;

		case 'D':
			{
				char	buff[128];

				if (getdomainname(buff,sizeof(buff)) < 0
					|| buff[0] == '\0'
					|| strcmp(buff, "(none)") == 0)
					break;
				putstr(buff);
			}
			break;

		case 'i':
			{
				char buff[3];
				FILE *fp;
				int p, c;

				if ((fp = fopen(ISSUE_FILE, "r")) == NULL)
					break;
				p = '\n';
				while ((c = fgetc(fp)) != EOF) {
					if (p == '\n' && c == '#') {
						do {
							c = fgetc(fp);
						} while (c != EOF && c != '\n');
						continue;
					} else if (c == '%') {
						buff[0] = c;
						c = fgetc(fp);
						if (c == EOF) break;
						buff[1] = c;
						buff[2] = '\0';
						putf(buff, NULL);
					} else {
						if (c == '\n') putchr('\r');
						putchr(c);
						p = c;
					}
				};
				(void) fclose(fp);
			}
			return; /* ignore remainder of the banner string */
			/*NOTREACHED*/

		case 's':
			putstr(kerninfo.sysname);
			break;

		case 'm':
			putstr(kerninfo.machine);
			break;

		case 'r':
			putstr(kerninfo.release);
			break;

		case 'v':
#ifdef __linux__
			putstr(kerninfo.version);
#else
			puts(kerninfo.version);
#endif
			break;
		}
		cp++;
	}
}


static struct buflist *
addbuf(const char *buf, size_t len)
{
	struct buflist *bufl;

	bufl = malloc(sizeof(struct buflist));
	if (!bufl) {
		return 0;
	}
	bufl->next = tail->next;
	bufl->buf = malloc(len);
	if (!bufl->buf) {
		free(bufl);
		return 0;
	}
	bufl->len = len;

	tail = tail->next = bufl;
	listlen++;

	memcpy(bufl->buf, buf, len);
	return bufl;
}

static ssize_t
netwrite(void *cookie, const char *buf, size_t len)
{
	size_t ret;
	const char *const end = buf + len;
	int ltrailing = trailing;
	int ldoclear = doclear;

#define	wewant(p)	((*p&0xff) == IAC) && \
				((*(p+1)&0xff) != EC) && ((*(p+1)&0xff) != EL)

	ret = 0;

	if (ltrailing) {
		const char *p;
		size_t l;
		size_t m = tail->len;

		p = nextitem(tail->buf, tail->buf + tail->len, buf, end);
		ltrailing = !p;
		if (ltrailing) {
			p = end;
		}

		l = p - buf;
		tail->len += l;
		tail->buf = realloc(tail->buf, tail->len);
		if (!tail->buf) {
			return -1;
		}

		memcpy(tail->buf + m, buf, l);
		buf += l;
		len -= l;
		ret += l;
		trailing = ltrailing;
	}

	if (ldoclear) {
		struct buflist *lpprev;

		skip = 0;
		lpprev = &head;
		for (;;) {
			struct buflist *lp;

			lp = lpprev->next;

			if (lp == &head) {
				tail = lpprev;
				break;
			}

			if (lp == tail && ltrailing) {
				break;
			}

			if (!wewant(lp->buf)) {
				lpprev->next = lp->next;
				listlen--;
				free(lp->buf);
				free(lp);
			} else {
				lpprev = lp;
			}
		}
	}

	while (len) {
		const char *p;
		size_t l;

		p = nextitem(buf, end, 0, 0);
		ltrailing = !p;
		if (ltrailing) {
			p = end;
		} else if (ldoclear) {
			if (!wewant(buf)) {
				l = p - buf;
				goto cont;
			}
		}

		l = p - buf;
		if (!addbuf(buf, l)) {
			return ret ? ret : -1;
		}
		trailing = ltrailing;

cont:
		buf += l;
		len -= l;
		ret += l;
	}

	netwritebuf();
	return ret;
}

void
netopen() {
	/*
	typedef struct {
		cookie_read_function_t *read;
		cookie_write_function_t *write;
		cookie_seek_function_t *seek;
		cookie_close_function_t *close;
	} cookie_io_functions_t;
	*/
	static const cookie_io_functions_t funcs = {
		//0, netwrite, 0, 0
		read: 0, write: netwrite, seek: 0, close: 0
	};

	netfile = fopencookie(0, "w", funcs);
}

extern int not42;
void
sendurg(const char *buf, size_t len) {
	if (!not42) {
		fwrite(buf, 1, len, netfile);
		return;
	}

	urg = addbuf(buf, len);
}

size_t
netbuflen(int flush) {
	if (flush) {
		netflush();
	}
	return listlen != 1 ? listlen : tail->len - skip;
}

/*
 * From: @(#)global.c	5.2 (Berkeley) 6/1/90
 */
char global_rcsid[] = 
  "$Id: global.c,v 1.4 1999/12/12 14:59:44 dholland Exp $";

/*
 * Allocate global variables.  
 */

#include "defs.h"
#include "ext.h"

/*
 * Telnet server variable declarations
 */
char	options[256];
char	do_dont_resp[256];
char	will_wont_resp[256];
int	linemode;	/* linemode on/off */


int	flowmode;	/* current flow control state */


slcfun	slctab[NSLC + 1];	/* slc mapping table */

char	*terminaltype;

/*
 * I/O data buffers, pointers, and counters.
 */
char	ptyobuf[BUFSIZ+NETSLOP], *pfrontp, *pbackp;

char	netibuf[BUFSIZ], *netip;

int	pcc, ncc;

FILE	*netfile;

int	pty, net;
int	SYNCHing;		/* we are in TELNET SYNCH mode */

struct _clocks clocks;

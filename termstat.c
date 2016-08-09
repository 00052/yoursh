#include "telnetd.h"

/*
 * local variables
 */
int def_tspeed = -1, def_rspeed = -1;
#ifdef	TIOCSWINSZ
int def_row = 0, def_col = 0;
#endif

/*
 * clientstat
 *
 * Process linemode related requests from the client.
 * Client can request a change to only one of linemode, editmode or slc's
 * at a time, and if using kludge linemode, then only linemode may be
 * affected.
 */
void clientstat(register int code, register int parm1, register int parm2)
{
	/*
	 * Get a copy of terminal characteristics.
	 */
	init_termbuf();

	/*
	 * Process request from client. code tells what it is.
	 */
	switch (code) {
	case TELOPT_NAWS:
#ifdef	TIOCSWINSZ
	    {
		struct winsize ws;

		def_col = parm1;
		def_row = parm2;

		/*
		 * Change window size as requested by client.
		 */

		ws.ws_col = parm1;
		ws.ws_row = parm2;
		(void) ioctl(pty, TIOCSWINSZ, (char *)&ws);
	    }
#endif	/* TIOCSWINSZ */
		
		break;
	
	case TELOPT_TSPEED:
	    {
		def_tspeed = parm1;
		def_rspeed = parm2;
		/*
		 * Change terminal speed as requested by client.
		 * We set the receive speed first, so that if we can't
		 * store seperate receive and transmit speeds, the transmit
		 * speed will take precedence.
		 */
		tty_rspeed(parm2);
		tty_tspeed(parm1);
		set_termbuf();

		break;

	    }  /* end of case TELOPT_TSPEED */

	default:
		/* What? */
		break;
	}  /* end of switch */

	netflush();

}  /* end of clientstat */



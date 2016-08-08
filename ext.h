/*
 * Telnet server variable declarations
 */
extern char options[256];
extern char do_dont_resp[256];
extern char will_wont_resp[256];
extern int linemode;	/* linemode on/off */


extern int flowmode;	/* current flow control state */




extern slcfun slctab[NSLC + 1];	/* slc mapping table */

extern char *terminaltype;

extern char *loginprg;

/*
 * I/O data buffers, pointers, and counters.
 */
extern char ptyobuf[BUFSIZ+NETSLOP], *pfrontp, *pbackp;
extern char netibuf[BUFSIZ], *netip;
extern int pcc, ncc;
extern FILE *netfile;

/* printf into netobuf */
#define netoprintf(fmt, ...) fprintf(netfile, fmt, ## __VA_ARGS__)

extern int pty, net;
extern const char *line;
extern int SYNCHing;		/* we are in TELNET SYNCH mode */

void _termstat(void);
void add_slc(int, int, int);
void check_slc(void);
void change_slc(int, int, int);
void cleanup(int) __attribute__ ((noreturn));
void clientstat(int, int, int);
void copy_termbuf(char *, int);
void deferslc(void);
void defer_terminit(void);
void do_opt_slc(unsigned char *, int);
void doeof(void);
void dooption(int);
void dontoption(int);
void edithost(const char *, const char *);
void fatal(int, const char *) __attribute__ ((noreturn));
void fatalperror(int, const char *) __attribute__ ((noreturn));
void get_slc_defaults(void);
void init_env(void);
void init_termbuf(void);
void interrupt(void);
void localstat(void);
void netclear(void);
void netflush(void);
size_t netbuflen(int);
void sendurg(const char *, size_t);


void ptyflush(void);
void putchr(int);
void putf(const char *, char *);
void recv_ayt(void);
void send_do(int, int);
void send_dont(int, int);
void send_slc(void);
void send_status(void);
void send_will(int, int);
void send_wont(int, int);
void sendbrk(void);
void sendsusp(void);
void set_termbuf(void);
void start_login(const char *, int, const char *);
void start_slc(int);
void startslave(const char *host, int autologin, char *autoname);

void start_slave(char *, int, char *);

void suboption(void);
void telrcv(void);
void ttloop(void);
void tty_binaryin(int);
void tty_binaryout(int);

int end_slc(unsigned char **);
int getnpty(void);
int getpty(void);
int login_tty(int);
int spcset(int, cc_t *, cc_t **);
int stilloob(int);
int terminit(void);
int termstat(void);
int tty_flowmode(void);
int tty_isbinaryin(void);
int tty_isbinaryout(void);
int tty_iscrnl(void);
int tty_isecho(void);
int tty_isediting(void);
int tty_islitecho(void);
int tty_isnewmap(void);
int tty_israw(void);
int tty_issofttab(void);
int tty_istrapsig(void);
int tty_linemode(void);

void tty_rspeed(int);
void tty_setecho(int);
void tty_setedit(int);
void tty_setlinemode(int);
void tty_setlitecho(int);
void tty_setsig(int);
void tty_setsofttab(int);
void tty_tspeed(int);
void willoption(int);
void wontoption(int);
#define writenet(b, l) fwrite(b, 1, l, netfile)
void netopen(void);



/*
 * The following are some clocks used to decide how to interpret
 * the relationship between various variables.
 */

extern struct _clocks {
    int system;			/* what the current time is */
    int echotoggle;		/* last time user entered echo character */
    int modenegotiated;		/* last time operating mode negotiated */
    int didnetreceive;		/* last time we read data from network */
    int ttypesubopt;		/* ttype subopt is received */
    int tspeedsubopt;		/* tspeed subopt is received */
    int environsubopt;		/* environ subopt is received */
    int xdisplocsubopt;		/* xdisploc subopt is received */
    int baseline;		/* time started to do timed action */
    int gotDM;			/* when did we last see a data mark */
} clocks;

#define DEFAULT_IM	"%i\r\n%s %r (%h) (%t)\r\n\r\n"

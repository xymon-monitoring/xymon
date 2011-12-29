#ifndef _TCPLIB_H_
#define _TCPLIB_H_

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#endif

enum conn_socktype_t { CONN_SOCKTYPE_STREAM, CONN_SOCKTYPE_DGRAM };
enum infolevel_t { INFO_CRITICAL, INFO_ERROR, INFO_WARN, INFO_INFO, INFO_DEBUG };
enum conn_callback_t { 
	CONN_CB_NEWCONNECTION, 		/* Server mode: New incoming connection accepted */
	CONN_CB_CONNECT_START, 		/* Client mode: connect() about to be called */
	CONN_CB_CONNECT_COMPLETE, 	/* Client mode: connect() succeeded */
	CONN_CB_CONNECT_FAILED,		/* Client mode: connect() failed */
	CONN_CB_SSLHANDSHAKE_OK,	/* Client/server mode: SSL handshake completed OK (peer certificate ready) */
	CONN_CB_SSLHANDSHAKE_FAILED,	/* Client/server mode: SSL handshake failed (connection will close) */
	CONN_CB_READCHECK, 		/* Client/server mode: Check if application wants to read data */
	CONN_CB_WRITECHECK,	 	/* Client/server mode: Check if application wants to write data */
	CONN_CB_READ, 			/* Client/server mode: Ready for application to read data w/ conn_read() */
	CONN_CB_WRITE,			/* Client/server mode: Ready for application to write data w/ conn_write() */
	CONN_CB_TIMEOUT,		/* Client/server mode: Timeout occurred */
	CONN_CB_CLOSED,			/* Client/server mode: Connection has been closed */
	CONN_CB_CLEANUP			/* Client/server mode: Connection cleanup */
};

extern char *conn_callback_names[];

typedef struct tcpconn_t {
	int sock;
	sa_family_t family;
	struct sockaddr *peer;
	void *peer_sin;
	int peersz;
	enum {
		CONN_PLAINTEXT, 
		CONN_SSL_INIT, CONN_SSL_CONNECTING, CONN_PLAINTEXT_CONNECTING,
		CONN_SSL_ACCEPT_READ, CONN_SSL_ACCEPT_WRITE, 
		CONN_SSL_CONNECT_READ, CONN_SSL_CONNECT_WRITE, 
		CONN_SSL_READ, CONN_SSL_WRITE, CONN_SSL_READY, 
		CONN_CLOSING, 
		CONN_DEAD
	} connstate;
	int maxlifetime;
	struct timespec starttime;
	long elapsedms;
	void *userdata;
	int (*usercallback)(struct tcpconn_t *, enum conn_callback_t, void *);
	struct tcpconn_t *next;
#ifdef HAVE_OPENSSL
	SSL_CTX *ctx;
	SSL *ssl;
#endif
} tcpconn_t;


extern char *conn_print_ip(tcpconn_t *conn);
extern char *conn_print_address(tcpconn_t *conn);
extern char *conn_peer_certificate(tcpconn_t *conn, time_t *certstart, time_t *certend);

extern int conn_listen(int portnumber, int backlog, int withssl, char *local4, char *local6, int (*usercallback)(tcpconn_t *, enum conn_callback_t, void *));
extern tcpconn_t *conn_accept(tcpconn_t *ls);

extern void clear_fdsets(fd_set *fdr, fd_set *fdw, int *maxfd);
extern void add_fd(int sock, fd_set *fds, int *maxfd);


extern void conn_register_infohandler(void (*cb)(time_t, const char *id, char *msg), enum infolevel_t level);

extern void conn_init_server(int portnumber, int backlog, 
			     char *certfn, char *keyfn, int sslportnumber, char *rootcafn, int requireclientcert,
			     char *local4, char *local6,
			     int (*usercallback)(tcpconn_t *, enum conn_callback_t, void *));

extern void conn_init_client(void);
extern tcpconn_t *conn_prepare_connection(char *ip, int portnumber, enum conn_socktype_t socktype, 
					  char *localaddr, int withssl, char *certfn, char *keyfn, long maxlifetime,
					  int (*usercallback)(tcpconn_t *, enum conn_callback_t, void *), void *userdata);

extern int conn_fdset(fd_set *fdread, fd_set *fdwrite);
extern void conn_process_listeners(fd_set *fdread);
extern void conn_process_active(fd_set *fdread, fd_set *fdwrite);
extern int conn_read(tcpconn_t *conn, void *buf, size_t sz);
extern int conn_write(tcpconn_t *conn, void *buf, size_t count);
extern void conn_close_connection(tcpconn_t *conn, char *direction);

extern int conn_active(void);
extern void conn_trimactive(void);

extern void conn_deinit(void);

extern int conn_lookup_portnumber(char *svcname, int defaultport);
extern char *conn_lookup_ip(char *hostname, int *portnumber);

#endif


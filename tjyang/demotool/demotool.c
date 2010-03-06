/*----------------------------------------------------------------------------*/
/*                    C   C O D E   S P E C I F I C A T I O N                 */
/*----------------------------------------------------------------------------*/
/*                                                                            */
/* NAME                                                                       */ 
/*   Hobbit demonstration tool                                                */
/*                                                                            */
/* REVISION HISTORY							      */
/*     2005-2008     Original code by Henrik Storner                          */
/*     03/04/2010    T.J. Yang   				              */
/*									      */
/*                                                                            */
/* USAGE								      */
/*                                                                            */
/*    1. demotool [--confdir=Not/tmp/confdemo] --srvip=192.168.1.1            */
/*                                                                            */
/*    [tjyang@f12 demotool]$ ./demotool --srvip=192.168.1.2                   */ 
/*   (Re)loading config from /tmp/democonf.				      */
/*   (Re)loading config from /tmp/democonf.				      */
/*   (Re)loading config from /tmp/democonf.				      */
/*                                                                            */
/*    2. Configure demotool directory with sample data.   		      */
/*  democonf                                                                  */  
/*  |-- demohost                                                              */ 
/*  |   |-- client							      */ 
/*  |   |-- client_df							      */ 
/*  |   |-- client_free							      */ 
/*  |   |-- client_ifconfig						      */ 
/*  |   |-- client_ifstat						      */ 
/*  |   |-- client_mount						      */ 
/*  |   |-- client_netstat						      */ 
/*  |   |-- client_osversion						      */ 
/*  |   |-- client_ports						      */ 
/*  |   |-- client_ps							      */ 
/*  |   |-- client_route						      */ 
/*  |   |-- client_top							      */ 
/*  |   |-- client_uname						      */ 
/*  |   |-- client_vmstat						      */ 
/*  |   `-- client_who							      */ 
/*  |-- http								      */ 
/*  |   |-- delay							      */ 
/*  |   |-- listen							      */ 
/*  |   `-- response							      */ 
/*  |-- pop3								      */ 
/*  |   |-- listen							      */ 
/*  |   `-- response							      */ 
/*  `-- smtp								      */ 
/*      |-- listen							      */ 
/*      `-- response                                                          */ 
/*                                                                            */ 
/*                                                                            */ 
/* DESCRIPTION								      */
/*                                                                            */ 
/* This tool fakes several hosts that can be tested by hobbit, both with      */
/* fake network services and fake client data. It is used to demonstrate      */
/* features in Hobbit.                                                        */
/*  what you'd do is to run it and then change the files to                   */
/*  simulate the various kinds of failures. E.g. change the		      */
/*  demohost/client_df file to show a filesystem being full. Or remove	      */
/*  the smtp/listen file to stop responding to that service.		      */
/*                                                                            */
/*       								      */
/*									      */
/* RETURN CODE								      */
/*   SUCCESS (=0) - function completed sucessfully			      */
/*   ERROR   (=1) - error. 						      */
/*   WARNING (=2) - warning... something's not quite right, but it's	      */
/*                      not serious enough to prevent installation.	      */
/*									      */
/* LICENSE 								      */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*									      */
/*									      */
/* COPYRIGHT                                                                  */
/* Copyright (C) 2005-2008 Henrik Storner <henrik@hswn.dk>                    */
/*									      */
/*									      */
/* ------------------------ HEADER  FILE DECLARATION -------------------------*/
		

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include "say.h"
#include "config.h"

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif


/* ---------------------------- CONSTANT DECLARATION -------------------------*/
/*									      */


int  SUCCESS,debug = 0;
int  ERROR   = 1;
int  WARNING = 2;

/* Where we put demo configuration files */
char *CONFIGDIR = "/tmp/democonf";  

struct sockaddr_in srvaddr;

volatile int reconfig = 1;         /* use volatile integer for speed ? */

typedef struct netsvc_t {
	int listenfd;
	int delay;
	char *response;
	int respsize;
	struct netsvc_t *next;
} netsvc_t;

netsvc_t *nethead = NULL;

typedef struct active_t {
	int fd;
	netsvc_t *svc;
	struct timeval rbegin;
	char *respbuf, *respptr;
	int readdone;
	int bytesleft;
	struct active_t *next;
} active_t;

active_t *acthead = NULL;

typedef struct client_t {
	time_t lastupd;
	char *hostname;
	char *ostype;
	time_t bootup;
	double minload, maxload;
	char *msg;
	struct client_t *next;
} client_t;

client_t *clihead = NULL;

static DIR *confdir = NULL;
struct dirent *dent = NULL;
static char *path = NULL;

/* ---------------------------- FUNCTION DEFINITION --------------------------*/

char *nextservice(char *dirname, char *svc)
{
  /*  INPUT : dierctory path string name and service name.
      OUTPUT:
  */
	struct stat st;
	char fn[PATH_MAX];
	FILE *fd;
	char *result;

	if (dirname) {
		if (confdir) closedir(confdir);
		if (path) free(path);
		confdir = opendir(dirname);
		path = strdup(dirname);
	}

	do {
		do { dent = readdir(confdir); } while (dent && (*(dent->d_name) == '.'));

		if (!dent) {
			closedir(confdir);
			free(path);
			path = NULL;
			confdir = NULL;
			dent = NULL;
			return NULL;
		}

		sprintf(fn, "%s/%s/%s", path, dent->d_name, svc);
	} while ( (stat(fn, &st) == -1) || ((fd = fopen(fn, "r")) == NULL) );

	result = (char *)malloc(st.st_size+1);
	fread(result, 1, st.st_size, fd);
	*(result + st.st_size) = '\0';
	fclose(fd);

	return result;
}

char *svcattrib(char *attr)
{
	struct stat st;
	char fn[PATH_MAX];
	FILE *fd;
	char *result;

	if (!dent) return NULL;

	if (!attr) {
		sprintf(fn, "%s/%s", path, dent->d_name);
		return strdup(fn);
	}

	sprintf(fn, "%s/%s/%s", path, dent->d_name, attr);
	if (stat(fn, &st) == -1) return NULL;
	fd = fopen(fn, "r"); if (!fd) return NULL;

	result = (char *)calloc(1, st.st_size+1);
	fread(result, 1, st.st_size, fd);
	fclose(fd);

	return result;
}

void addtobuffer(char **buf, int *bufsz, char *newtext)
{
	if (*buf == NULL) {
		*bufsz = strlen(newtext) + 4096;
		*buf = (char *) malloc(*bufsz);
		**buf = '\0';
	}
	else if ((strlen(*buf) + strlen(newtext) + 1) > *bufsz) {
		*bufsz += strlen(newtext) + 4096;
		*buf = (char *) realloc(*buf, *bufsz);
	}

	strcat(*buf, newtext);
}
/* WHAT: */ 
char *clientdata(char *cpath)
{
	char *res = NULL;
	int ressz = 0;
	DIR *cdir;
	struct dirent *d;
	char fn[PATH_MAX];
	struct stat st;
	int n;
	FILE *fd;
	char buf[4096];

	cdir = opendir(cpath);
	while ((d = readdir(cdir)) != NULL) {
		if (strncmp(d->d_name, "client_", 7) != 0) continue;

		sprintf(fn, "%s/%s", cpath, d->d_name);
		if (stat(fn, &st) == -1) continue;
		fd = fopen(fn, "r"); if (fd == NULL) continue;

		sprintf(buf, "[%s]\n", d->d_name+7);
		addtobuffer(&res, &ressz, buf);

		while ((n = fread(buf, 1, sizeof(buf)-1, fd)) > 0) {
			*(buf+n) = '\0';
			addtobuffer(&res, &ressz, buf);
		}

		fclose(fd);
	}
	closedir(cdir);

	if (!res) res = strdup("");

	return res;
}

int timeafter(struct timeval *lim, struct timeval *now)
{
	if (now->tv_sec > lim->tv_sec) return 1;
	if (now->tv_sec < lim->tv_sec) return 0;
	return (now->tv_usec >= lim->tv_usec);
}

void setuplisteners(void)
{
	netsvc_t *nwalk;
	char *lspec;
	struct sockaddr_in laddr;

	nwalk = nethead;
	while (nwalk) {
		netsvc_t *tmp = nwalk;
		nwalk = nwalk->next;

		if (tmp->listenfd > 0) close(tmp->listenfd);
		if (tmp->response) free(tmp->response);
		free(tmp);
	}
	nethead = NULL;

	lspec = nextservice(CONFIGDIR, "listen");
	while (lspec) {
		char *p, *listenip = NULL;
		int listenport = -1;
		int lsocket, opt;

		p = strchr(lspec, ':'); if (p) { *p = '\0'; p++; listenip = lspec; listenport = atoi(p); }

		if (listenip && (listenport > 0)) {
 			memset(&laddr, 0, sizeof(laddr));
			inet_aton(listenip, (struct in_addr *) &laddr.sin_addr.s_addr);
			laddr.sin_port = htons(listenport);
			laddr.sin_family = AF_INET;
			lsocket = socket(AF_INET, SOCK_STREAM, 0);

			if (lsocket != -1) {
				opt = 1;
				setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
				fcntl(lsocket, F_SETFL, O_NONBLOCK);

				if ( (bind(lsocket, (struct sockaddr *)&laddr, sizeof(laddr)) != -1) &&
				     (listen(lsocket, 10) != -1) ) {
					netsvc_t *newitem = malloc(sizeof(netsvc_t));

					newitem->listenfd = lsocket;
					newitem->response = svcattrib("response");
					newitem->respsize = (newitem->response ? strlen(newitem->response) : 0);
					p = svcattrib("delay");
					newitem->delay = (p ? atoi(p) : 0);
					newitem->next = nethead;
					nethead = newitem;
				}
			}
		}

		free(lspec);
		lspec = nextservice(NULL, "listen");
	}
}

void setupclients(void)
{
	client_t *cwalk;
	char *cspec;

	cwalk = clihead;
	while (cwalk) {
		client_t *tmp = cwalk;
		cwalk = cwalk->next;

		if (tmp->hostname) free(tmp->hostname);
		if (tmp->ostype) free(tmp->ostype);
		if (tmp->msg) free(tmp->msg);
		free(tmp);
	}
	clihead = NULL;

	cspec = nextservice(CONFIGDIR, "client");
	while (cspec) {
		char *p;

		p = strchr(cspec, ':');
		if (p) {
			client_t *newitem = (client_t *)malloc(sizeof(client_t));

			*p = '\0';
			newitem->lastupd = 0;
			newitem->hostname = strdup(cspec);
			newitem->ostype = strdup(p+1);
			while ((p = strchr(newitem->hostname, '.')) != NULL) *p = ',';
			p = svcattrib("uptime");
			if (p) newitem->bootup = time(NULL) - 60*atoi(p);
			else newitem->bootup = time(NULL);
			p = svcattrib("minload");
			if (p) newitem->minload = atof(p); else newitem->minload = 0.2;
			p = svcattrib("maxload");
			if (p) newitem->maxload = atof(p); else newitem->maxload = 1.0;
			newitem->msg = clientdata(svcattrib(NULL));
			newitem->next = clihead;
			clihead = newitem;
		}

		free(cspec);
		cspec = nextservice(NULL, "client");
	}
}

void do_select(void)
{
	fd_set readfds, writefds;
	int maxfd, n;
	netsvc_t *nwalk;
	active_t *awalk, *aprev;
	struct timeval now, start;
	struct timezone tz;
	struct timeval tmo;
	char rbuf[4096];

	gettimeofday(&start, &tz);
	do {
		gettimeofday(&now, &tz);
		FD_ZERO(&readfds); FD_ZERO(&writefds); maxfd = -1;

		nwalk = nethead;
		while (nwalk) {
			if (nwalk->listenfd) {
				FD_SET(nwalk->listenfd, &readfds);
				if (nwalk->listenfd > maxfd) maxfd = nwalk->listenfd;
			}
			nwalk = nwalk->next;
		}

		awalk = acthead;
		while (awalk) {
			if (awalk->fd) {
				if (!awalk->readdone) {
					FD_SET(awalk->fd, &readfds);
					if (awalk->fd > maxfd) maxfd = awalk->fd;
				}
				if (timeafter(&awalk->rbegin, &now)) {
					FD_SET(awalk->fd, &writefds);
					if (awalk->fd > maxfd) maxfd = awalk->fd;
				}
			}
			awalk = awalk->next;
		}

		tmo.tv_sec = 0; tmo.tv_usec = 100000;
		n = select(maxfd+1, &readfds, &writefds, NULL, &tmo);
	} while ((n == 0) && ((now.tv_sec - start.tv_sec) < 10));

	if (n <= 0) return;

	gettimeofday(&now, &tz);

	awalk = acthead; aprev = NULL;
	while (awalk) {
		if (awalk->fd && !awalk->readdone && FD_ISSET(awalk->fd, &readfds)) {
			n = read(awalk->fd, rbuf, sizeof(rbuf));
			if (n <= 0) awalk->readdone = 1;
		}

		if (awalk->fd && awalk->respptr && FD_ISSET(awalk->fd, &writefds)) {
			n = write(awalk->fd, awalk->respptr, awalk->bytesleft);
			if (n > 0) { awalk->respptr += n; awalk->bytesleft -= n; }
		}
		else n = 0;

		if ((awalk->bytesleft == 0) || (n < 0)) {
			if (awalk->respbuf) free(awalk->respbuf);
			close(awalk->fd);
			awalk->fd = 0;
		}

		if (awalk->fd) {
			aprev = awalk; awalk = awalk->next;
		}
		else {
			active_t *tmp = awalk;

			awalk = awalk->next;

			if (aprev) aprev->next = awalk;
			else acthead = awalk;

			free(tmp);
		}
	}

	nwalk = nethead;
	while (nwalk) {
		int newfd;

		if (nwalk->listenfd && FD_ISSET(nwalk->listenfd, &readfds)) {
			while ((newfd = accept(nwalk->listenfd, NULL, 0)) > 0) {
				/* Pick up a new connection */
				fcntl(newfd, F_SETFL, O_NONBLOCK);
				active_t *newitem = (active_t *)malloc(sizeof(active_t));
				newitem->fd = newfd;
				newitem->rbegin.tv_sec = now.tv_sec + nwalk->delay;
				newitem->rbegin.tv_usec = now.tv_usec + (random() % 1000000);
				if (newitem->rbegin.tv_usec > 1000000) {
					newitem->rbegin.tv_usec -= 1000000;
					newitem->rbegin.tv_sec++;
				}
				newitem->svc = nwalk;
				if (nwalk->response) {
					newitem->respbuf = strdup(nwalk->response);
					newitem->respptr = newitem->respbuf;
					newitem->bytesleft = nwalk->respsize;
				}
				else {
					newitem->respbuf = NULL;
					newitem->respptr = NULL;
					newitem->bytesleft = 0;
				}
				newitem->readdone = 0;
				newitem->next = acthead;
				acthead = newitem;
			}
		}
		nwalk = nwalk->next;
	}
}

void do_clients(void)
{
	client_t *cwalk;
	active_t *conn;
	time_t now = time(NULL);
	time_t uptime;
	int updays, uphours, upmins;
	int n;
	struct tm *tm;
	double curload;

	cwalk = clihead;
	while (cwalk && ((cwalk->lastupd + 60) > now)) cwalk = cwalk->next;
	if (!cwalk) return;

	cwalk->lastupd = now;
	tm = localtime(&now);
	uptime = now - cwalk->bootup;
	updays = uptime / 86400;
	uphours = (uptime % 86400) / 3600;
	upmins = (uptime % 3600) / 60;
	curload = cwalk->minload + ((random() % 1000) / 1000.0) * (cwalk->maxload - cwalk->minload);

	conn = malloc(sizeof(active_t));
	conn->fd = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(conn->fd, F_SETFL, O_NONBLOCK);
	conn->rbegin.tv_sec = now;
	conn->rbegin.tv_usec = 0;
	conn->svc = NULL;
	conn->respbuf = (char *)malloc(strlen(cwalk->msg) + 4096);
	sprintf(conn->respbuf, "client %s.%s\n[date]\n%s[uptime]\n %02d:%02d:%02d up %d days, %d:%02d, 1 users, load average: 0.21, %5.2f, 0.25\n\n%s\n",
		cwalk->hostname, cwalk->ostype, 
		ctime(&now),
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		updays, uphours, upmins,
		curload, cwalk->msg);
	conn->respptr = conn->respbuf;
	conn->bytesleft = strlen(conn->respbuf);
	conn->readdone = 0;

	n = connect(conn->fd, (struct sockaddr *)&srvaddr, sizeof(srvaddr));

	if ((n == 0) || ((n == -1) && (errno == EINPROGRESS))) {
		conn->next = acthead;
		acthead = conn;
	}
	else {
		close(conn->fd);
		free(conn->respbuf);
		free(conn);
	}
}

void sigmisc_handler(int signum)
{
	if (signum == SIGHUP) reconfig = 1;
}

/* ---------------------------------- MAIN  ----------------------------------*/											   
int main(int argc, char *argv[])
{
  int argi; 
  struct sigaction sa;
  time_t lastconf = 0;
  int showhelp = 0;

  memset(&srvaddr, 0, sizeof(srvaddr));
  /* Xymon server port number */
  srvaddr.sin_port = htons(1984);   
  srvaddr.sin_family = AF_INET;
  /* */
  inet_aton("127.0.0.1", (struct in_addr *) &srvaddr.sin_addr.s_addr);

  /* Handle program options. */

  if (argc < 2){
	     /* No more options - pickup recipient and msg */
	     showhelp=1;
  }

  if (showhelp) {
    fprintf(stderr, "Xymon demotool version %s\n", VERSION);
    fprintf(stderr, "Usage: %s  --srvip=192.168.1.2 [confdir=/tmp/demo] [--debug]\n", argv[0]);
    fprintf(stderr, " confdir default set to /tmp/demo.\n");
    return ERROR;
   }  

  for (argi = 1; (argi < argc); argi++) { 	 
  	if (strncmp(argv[argi], "--confdir=", 10) == 0) {
  		char *p = strchr(argv[argi], '=');
  		CONFIGDIR = strdup(p+1);
  	}
  	else if (strncmp(argv[argi], "--srvip=", 8) == 0) {
  	     char *p = strchr(argv[argi], '=');
  	     inet_aton(p+1, (struct in_addr *) &srvaddr.sin_addr.s_addr);
  	}
	else if (strcmp(argv[argi], "--debug") == 0) {
	    /*
	     * A global "debug" variable is available. If
	     */
	    debug = 1;
	}
  }


  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigmisc_handler;
  sigaction(SIGHUP, &sa, NULL);
  
  while (1) {
  	if (reconfig || (time(NULL) > (lastconf+60))) {
	  printf("(Re)loading config from %s. \n",CONFIGDIR);
  		setuplisteners();
  		setupclients();
  		reconfig = 0;
  		lastconf = time(NULL);
  	}
  
  	do_clients();
  	do_select();
  }
  return SUCCESS ;
}


typedef struct {
	char *svcname;
	char *sendtxt;
	int  grabbanner;
} svcinfo_t;

typedef struct {
	struct sockaddr_in addr;        /* Address (IP+port) to test */
	int  fd;                        /* Socket filedescriptor */
	int  open;                      /* Result - is it open? */
	int  connres;                   /* connect() status returned */
	struct timeval timestart, duration;
	svcinfo_t *svcinfo;             /* Points to svcinfo_t for service */
	int  silenttest;
	int  readpending;               /* Temp status while reading banner */
	char *banner;                   /* Banner text from service */
	void *next;
} test_t;

extern test_t *add_test(char *ip, int portnum, char *service, int silent);
extern void do_conn(int timeout, int concurrency);
extern void show_conn_res(void);


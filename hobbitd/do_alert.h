#ifndef __BBD_ALERT_H__
#define __BBD_ALERT_H__

#include <time.h>

typedef struct htnames_t {
	char *name;
	struct htnames_t *next;
} htnames_t;

enum astate_t { A_PAGING, A_ACKED, A_RECOVERED, A_DEAD };

typedef struct activealerts_t {
	/* Identification of the alert */
	htnames_t *hostname;
	htnames_t *testname;
	htnames_t *location;

	/* Alert status */
	int color;
	unsigned char *pagemessage;
	unsigned char *ackmessage;
	time_t eventstart;
	time_t nextalerttime;
	enum astate_t state;

	struct activealerts_t *next;
} activealerts_t;

extern void load_alertconfig(char *configfn, int alertcolors);
extern void dump_alertconfig(void);
extern time_t next_alert(activealerts_t *alert);
extern void cleanup_alert(activealerts_t *alert);
extern void clear_interval(activealerts_t *alert);

extern void start_alerts(void);
extern void send_alert(activealerts_t *alert);
extern void finish_alerts(void);

extern void load_state(char *filename);
extern void save_state(char *filename);

#endif


#ifndef __BBD_ALERT_H__
#define __BBD_ALERT_H__

#include <sys/time.h>

typedef struct htnames_t {
	char *name;
	struct htnames_t *next;
} htnames_t;

enum astate_t { A_PAGING, A_ACKED, A_RECOVERED, A_DEAD };

typedef struct activealerts_t {
	/* Identification of the alert */
	htnames_t *hostname;
	htnames_t *testname;

	/* Alert status */
	int color;
	char *pagemessage;
	char *ackmessage;
	time_t eventstart;
	time_t nextalerttime;
	enum astate_t state;

	struct activealerts_t *next;
} activealerts_t;


extern void send_alert(activealerts_t *awalk);
extern time_t next_alert(activealerts_t *awalk);

#endif


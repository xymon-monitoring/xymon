/*----------------------------------------------------------------------------*/
/* SMS message GUI                                                            */
/*                                                                            */
/* Copyright (C) 2009 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: smsadmin.c,v 1.6 2009/06/30 14:36:51 henrik Exp henrik $";

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <utime.h>
#include <errno.h>

#include "libxymon.h"

#define MAX_RECIPIENTS 40
#define DEFAULT_LIFETIME 10080	/* 1 week in minutes (7 x 24 x 60) */

static void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

typedef enum { 
	ACT_DEFAULT,
	ACT_DELETE_MESSAGE, ACT_EDIT_MESSAGE, ACT_UPDATE_MESSAGE, ACT_VIEWLOG_MESSAGE,
	ACT_DELETE_RECIPLIST, ACT_EDIT_RECIPLIST, ACT_UPDATE_RECIPLIST,
	ACT_VIEW_FINISHED, ACT_VIEW_SUMMARY, ACT_REACTIVATE_MESSAGE
} action_t;
char *recipientlistname = NULL;
char *smstext = NULL;
char *descrtext = NULL;
int repeatinterval = 30;	/* Minutes */
int lifetime = DEFAULT_LIFETIME;
char *username = NULL;
char *realusername = NULL;
char *selectedqueue = "active";
char *selectedmsg = NULL;
char *selectedgroup = NULL;
int sendimmediately = 0;
char *recipnames[MAX_RECIPIENTS] = { NULL, };
char *recipnumbers[MAX_RECIPIENTS] = { NULL, };
char msgtextenv[2048];

action_t parse_query(void)
{
	cgidata_t *cgidata, *cwalk;
	int returnval = ACT_DEFAULT;
	int defaultact = ACT_EDIT_MESSAGE;

	cgidata = cgi_request();
	if (cgi_method != CGI_POST) return defaultact;

	if (cgidata == NULL) errormsg(cgi_error());

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcmp(cwalk->name, "reciplist") == 0) {
			if (cwalk->value && *(cwalk->value)) recipientlistname = cwalk->value;
			defaultact = ACT_EDIT_MESSAGE;
		}
		else if (strcmp(cwalk->name, "repeat") == 0) {
			repeatinterval = atoi(cwalk->value);
			defaultact = ACT_EDIT_MESSAGE;
		}
		else if (strcmp(cwalk->name, "lifetime") == 0) {
			lifetime = atoi(cwalk->value);
		}
		else if (strcmp(cwalk->name, "smstext") == 0) {
			if (cwalk->value && *(cwalk->value) && (*(cwalk->value) != '&')) smstext = cwalk->value;
		}
		else if (strcmp(cwalk->name, "Description") == 0) {
			if (cwalk->value && *(cwalk->value) && (*(cwalk->value) != '&')) descrtext = cwalk->value;
		}
		else if (strcmp(cwalk->name, "queuename") == 0) {
			if (cwalk->value && *(cwalk->value)) selectedqueue = cwalk->value;
			defaultact = ACT_EDIT_MESSAGE;
		}
		else if (strcmp(cwalk->name, "msglist") == 0) {
			if (cwalk->value && *(cwalk->value)) selectedmsg = cwalk->value;
			defaultact = ACT_EDIT_MESSAGE;
		}
		else if (strcmp(cwalk->name, "grouplist") == 0) {
			if (cwalk->value && *(cwalk->value)) selectedgroup = cwalk->value;
			defaultact = ACT_EDIT_MESSAGE;
		}
		else if (strcmp(cwalk->name, "DeleteMsgSubmit") == 0) {
			returnval = ACT_DELETE_MESSAGE;
		}
		else if (strcmp(cwalk->name, "EditMsgSubmit") == 0) {
			returnval = ACT_EDIT_MESSAGE;
		}
		else if (strcmp(cwalk->name, "ViewLogSubmit") == 0) {
			returnval = ACT_VIEWLOG_MESSAGE;
		}
		else if (strcmp(cwalk->name, "ReactivateSubmit") == 0) {
			returnval = ACT_REACTIVATE_MESSAGE;
		}
		else if (strcmp(cwalk->name, "SendSMS") == 0) {
			returnval = ACT_UPDATE_MESSAGE;
			sendimmediately =  1;
		}
		else if (strcmp(cwalk->name, "UpdateQueue") == 0) {
			returnval = ACT_UPDATE_MESSAGE;
			sendimmediately =  0;
		}
		else if (strcmp(cwalk->name, "ViewFinished") == 0) {
			returnval = ACT_VIEW_FINISHED;
		}
		else if (strcmp(cwalk->name, "ViewSummary") == 0) {
			returnval = ACT_VIEW_SUMMARY;
		}

		else if (strcmp(cwalk->name, "recipchoice") == 0) {
			if (cwalk->value && *(cwalk->value)) recipientlistname = cwalk->value;
			defaultact = ACT_EDIT_RECIPLIST;
		}
		else if (strncmp(cwalk->name, "RecipName_", 10) == 0) {
			int idx = atoi(cwalk->name+10);
			if ((idx >= 0) && (idx < (sizeof(recipnames) / sizeof(recipnames[0]))) && cwalk->value && *(cwalk->value)) recipnames[idx] = cwalk->value;
		}
		else if (strncmp(cwalk->name, "RecipNumber_", 12) == 0) {
			int idx = atoi(cwalk->name+12);
			if ((idx >= 0) && (idx < (sizeof(recipnumbers) / sizeof(recipnumbers[0]))) && cwalk->value && *(cwalk->value)) recipnumbers[idx] = cwalk->value;
		}
		else if (strcmp(cwalk->name, "GoRecipList") == 0) {
			returnval = ACT_EDIT_RECIPLIST;
		}
		else if (strcmp(cwalk->name, "UpdateRecipList") == 0) {
			returnval = ACT_UPDATE_RECIPLIST;
		}
		else if (strcmp(cwalk->name, "DeleteRecipList") == 0) {
			returnval = ACT_DELETE_RECIPLIST;
		}

		cwalk = cwalk->next;
	}

	return (returnval == ACT_DEFAULT) ? defaultact : returnval;
}


static int recipcompare(const void *v1, const void *v2)
{
	char **n1 = (char **)v1;
	char **n2 = (char **)v2;

	return strcmp(*n1, *n2);
}

static int rseqcompare(const void *v1, const void *v2)
{
	int *n1 = (int *)v1;
	int *n2 = (int *)v2;

	return strcmp(recipnames[*n1], recipnames[*n2]);
}


void setup_recipientlist_selection(char *listname, int listeditor)
{
	DIR *dirfd;
	struct dirent *d;
	struct stat st;
	int mustselect = 1;
	char **namelist;
	int i, lsize;

	sethostenv_clearlist(listname);
	if (listeditor) {
		sethostenv_addtolist(listname, "-- Create new list --", "", NULL, (recipientlistname == NULL));
		if (recipientlistname == NULL) mustselect = 0;	/* Already have selected the "create new list" entry */
	}

	dirfd = opendir("recips");
	if (dirfd == NULL) {
		mkdir("recips", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		return;
	}

	namelist = (char **)calloc(1, sizeof(char *));
	lsize = 0;

	while ((d = readdir(dirfd)) != NULL) {
		char fn[PATH_MAX];
		int selected;

		snprintf(fn, sizeof(fn), "recips/%s", d->d_name);
		if (stat(fn, &st) != 0) continue;
		if (!S_ISREG(st.st_mode)) continue;

		namelist[lsize++] = strdup(d->d_name);
		namelist = (char **)realloc(namelist, (lsize+1)*sizeof(char *));
		namelist[lsize] = NULL;
	}

	closedir(dirfd);

	qsort(&namelist[0], lsize, sizeof(char **), recipcompare);
	for (i = 0; namelist[i]; i++) {
		int selected;

		selected = (recipientlistname ? (strcmp(namelist[i], recipientlistname) == 0) : mustselect);
		mustselect = 0;

		sethostenv_addtolist(listname, namelist[i], namelist[i], NULL, selected);
		free(namelist[i]);
	}

	free(namelist);
}


void setup_smsgroup_selection(char **groups)
{
	static char *listname = "_SMSGROUP";
	int groupi;

	sethostenv_clearlist(listname);
	for (groupi = 0; (groups[groupi]); groupi++) {
		int selected = (strcmp(groups[groupi], username) == 0);
		sethostenv_addtolist(listname, groups[groupi], groups[groupi], NULL, selected);
	}
}

void setup_smslist_selection(void)
{
	static char *listname = "_MSGS";
	DIR *dirfd;
	struct dirent *d;
	struct stat st;

	sethostenv_clearlist(listname);

	dirfd = opendir("active");
	if (dirfd == NULL) {
		mkdir("active", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		return;
	}

	sethostenv_addtolist(listname, "-- Create new message --", "", NULL, (selectedmsg == NULL));

	while ((d = readdir(dirfd)) != NULL) {
		char fn[PATH_MAX];
		FILE *fd;
		char infotxt[100];
		int selected;

		snprintf(fn, sizeof(fn), "active/%s", d->d_name);
		if (stat(fn, &st) != 0) continue;
		if (!S_ISDIR(st.st_mode)) continue;

		if (strncmp(d->d_name, "sms", 3) != 0) continue;

		infotxt[0] = '\0';
		strcat(fn, "/info");
		fd = fopen(fn, "r"); 
		if (fd) {
			fgets(infotxt, sizeof(infotxt), fd);
			fclose(fd);
		}
		selected = (selectedmsg ? (strcmp(d->d_name, selectedmsg) == 0) : 0);
		sethostenv_addtolist(listname, infotxt, d->d_name, NULL, selected);
	}

	closedir(dirfd);
}


void setup_repeat_selection(void)
{
	static char *listname = "_REPEAT";

	sethostenv_clearlist(listname);
	sethostenv_addtolist(listname, "15 minutes", "15", NULL, (repeatinterval == 15));
	sethostenv_addtolist(listname, "30 minutes", "30", NULL, (repeatinterval == 30));
	sethostenv_addtolist(listname, "1 hour", "60", NULL, (repeatinterval == 60));
	sethostenv_addtolist(listname, "2 hours", "120", NULL, (repeatinterval == 120));
	sethostenv_addtolist(listname, "3 hours", "180", NULL, (repeatinterval == 180));
	sethostenv_addtolist(listname, "6 hours", "360", NULL, (repeatinterval == 360));
	sethostenv_addtolist(listname, "12 hours", "720", NULL, (repeatinterval == 720));
	sethostenv_addtolist(listname, "Suspended", "-1", NULL, (repeatinterval == -1));
	sethostenv_addtolist(listname, "No repeat", "-2", NULL, (repeatinterval == -2));
}

void setup_lifetime_selection(int fixed)
{
	static char *listname = "_LIFETIME";

	sethostenv_clearlist(listname);
	if (fixed) {
		char txt[10], val[10];

		sprintf(txt, "%d hours", lifetime/60);
		sprintf(val, "%d", lifetime);
		sethostenv_addtolist(listname, txt, val, NULL, 1);
	}
	else {
		sethostenv_addtolist(listname, "3 hours", "180", NULL, (lifetime == 180));
		sethostenv_addtolist(listname, "6 hours", "360", NULL, (lifetime == 360));
		sethostenv_addtolist(listname, "9 hours", "540", NULL, (lifetime == 540));
		sethostenv_addtolist(listname, "12 hours", "720", NULL, (lifetime == 720));
		sethostenv_addtolist(listname, "24 hours", "1440", NULL, (lifetime == 1440));
		sethostenv_addtolist(listname, "36 hours", "2160", NULL, (lifetime == 2160));
		sethostenv_addtolist(listname, "48 hours", "2880", NULL, (lifetime == 2880));
		sethostenv_addtolist(listname, "1 week", "10080", NULL, (lifetime == 10080));
	}
}


char *tell_io_error(char *txt, char *v)
{
	static char errmsg[500];

	sprintf(errmsg, "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('%s (%s): %s'); </SCRIPT>\n",
		txt, (v ? v : ""), strerror(errno));
	return errmsg;
}


typedef struct activelist_t {
	char *user, *id, *info;
	time_t lastxmit;
	int repeat;
	struct activelist_t *next;
} activelist_t;

static int activecompare(const void *v1, const void *v2)
{
	activelist_t **a1 = (activelist_t **)v1;
	activelist_t **a2 = (activelist_t **)v2;
	time_t n1, n2;

	n1 = (*a1)->lastxmit + 60*((*a1)->repeat);
	n2 = (*a2)->lastxmit + 60*((*a2)->repeat);

	if (n1 < n2) return -1;
	else if (n1 > n2) return 1;
	else return 0;
}

void show_summary(char *topdirectory, char *queuename, FILE *outfd, char **groups)
{
	activelist_t *acthead = NULL, *actwalk;
	int actcount = 0;
	activelist_t **activelist = NULL;
	int acti, groupi;
	struct stat st;

	dbgprintf("show_summary: queuename=%s\n", queuename);

	for (groupi = 0; (groups[groupi]); groupi++) {
		char userdir[PATH_MAX];
		char *curruser;
		char activedir[PATH_MAX];
		DIR *activedirfd;
		struct dirent *activedird;

		snprintf(userdir, sizeof(userdir)-1, "%s/%s", topdirectory, groups[groupi]);
		if (stat(userdir, &st) != 0) continue;
		if (!S_ISDIR(st.st_mode)) continue;
		curruser = groups[groupi];

		sprintf(activedir, "%s/%s", userdir, queuename);
		dbgprintf("show_summary: scanning directory %s\n", activedir);
		activedirfd = opendir(activedir);
		while (activedirfd && ((activedird = readdir(activedirfd)) != NULL)) {
			char smsdir[PATH_MAX];
			char fn[PATH_MAX];
			FILE *fd;
			char infobuf[1024];
			time_t lastxmit = 0;
			int repeat = 30;
			activelist_t *newentry;

			if (strncmp(activedird->d_name, "sms", 3) != 0) continue;
			snprintf(smsdir, sizeof(smsdir)-1, "%s/%s", activedir, activedird->d_name);
			if (stat(smsdir, &st) != 0) continue;
			if (!S_ISDIR(st.st_mode)) continue;

			/* Get the info data */
			sprintf(fn, "%s/%s", smsdir, "info");
			if (stat(fn, &st) != 0) continue;
			fd = fopen(fn, "r");
			if (fd) {
				fgets(infobuf, sizeof(infobuf)-1, fd);
				fclose(fd);
			}
			else {
				strcpy(infobuf, "Untitled entry");
			}

			/* Get the time of the next transmission */
			sprintf(fn, "%s/%s", smsdir, "lastxmit");
			if (stat(fn, &st) != 0) continue;
			lastxmit = st.st_mtime;

			sprintf(fn, "%s/%s", smsdir, "repeat");
			fd = fopen(fn, "r");
			if (fd) {
				char buf[100];

				if (fgets(buf, sizeof(buf)-1, fd)) repeat = atoi(buf);
				fclose(fd);
			}

			newentry = (activelist_t *)calloc(1, sizeof(activelist_t));
			newentry->user = strdup(curruser);
			newentry->id = strdup(activedird->d_name);
			newentry->info = strdup(infobuf);
			newentry->lastxmit = lastxmit;
			newentry->repeat = (strcmp(queuename, "active") == 0) ? repeat : 0;
			newentry->next = acthead;
			acthead = newentry;
			actcount++;
		}

		if (activedirfd) closedir(activedirfd);
	}

	activelist = (activelist_t **)calloc(actcount, sizeof(activelist_t **));
	for (actwalk = acthead, acti = 0; actwalk; actwalk = actwalk->next, acti++) activelist[acti] = actwalk;
	qsort(&activelist[0], actcount, sizeof(activelist_t **), activecompare);

	fprintf(outfd, "<form action=\"\" method=\"POST\">\n");
	fprintf(outfd, "<input type=\"hidden\" name=\"queuename\" id=\"queuename\" value=\"\">");
	fprintf(outfd, "<input type=\"hidden\" name=\"grouplist\" id=\"grouplist\" value=\"\">");
	fprintf(outfd, "<input type=\"hidden\" name=\"msglist\" id=\"msglist\" value=\"\">");

	fprintf(outfd, "<table width=\"90%%\" align=\"center\">\n");
	fprintf(outfd, "<tr><th align=left>User</th><th align=left>Description</th><th align=left>%s transmission</th></tr>\n",
		(strcmp(queuename, "active") == 0) ? "Next" : "Last");
	for (acti = 0; (acti < actcount); acti++) {
		char nextstr[30];

		if (activelist[acti]->repeat <= 0) {
			strcpy(nextstr, "Suspended");
		}
		else if (activelist[acti]->lastxmit <= 0) {
			strcpy(nextstr, "Now");
		}
		else {
			time_t nextxmit = activelist[acti]->lastxmit + 60*activelist[acti]->repeat;
			strftime(nextstr, sizeof(nextstr), "%H:%M %Y-%m-%d", localtime(&nextxmit));
		}

		fprintf(outfd, "<tr>");
		fprintf(outfd, "<td>%s</td><td>%s</td><td>%s</td>", activelist[acti]->user, activelist[acti]->info, nextstr);
		if (strcmp(queuename, "active") == 0) {
			fprintf(outfd, "<td><input type=\"submit\" name=\"EditMsgSubmit\" id=\"EditMsgSubmit\" onClick=\"return editSms('%s','%s','%s');\" value=\"Edit message\"></td>",
				queuename, activelist[acti]->user, activelist[acti]->id);
		}
		else if (strcmp(queuename, "old") == 0) {
			fprintf(outfd, "<td><input type=\"submit\" name=\"ViewLogSubmit\" id=\"ViewLogSubmit\" onClick=\"return editSms('%s','%s','%s');\" value=\"View log\"></td>",
				queuename, activelist[acti]->user, activelist[acti]->id);
			fprintf(outfd, "<td><input type=\"submit\" name=\"ReactivateSubmit\" id=\"ReactivateSubmit\" onClick=\"return editSms('%s','%s','%s');\" value=\"Reactivate\"></td>",
				queuename, activelist[acti]->user, activelist[acti]->id);
		}
		fprintf(outfd, "</tr>\n");
	}
	fprintf(outfd, "</table>\n");
	fprintf(outfd, "</form>\n");
}


char **getgrouplist(char *groupfn, char *userid)
{
	FILE *fd;
	char buf[4096];
	char **result;
	int count;
	char *gname; 

	count = 0;
	result = (char **)calloc(2, sizeof(char *));
	result[count++] = strdup(userid);

	fd = fopen(groupfn, "r");
	if (!fd) return result;
	while (fgets(buf, sizeof(buf), fd)) {
		int found = 0;
		char *member = NULL;

		gname = strtok(buf, ":");
		do {
			member = strtok(NULL, " \r\n");
			found = (member && (strcmp(userid, member) == 0));
			if (found) {
				result = (char **)realloc(result, (count+2)*sizeof(char *));
				result[count++] = strdup(gname);
			}
		} while (member && !found);
	}
	fclose(fd);

	result[count] = NULL;
	return result;
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *hffile = "sms";
	int bgcolor = COL_BLUE;
	char *infomsg = NULL;
	char userdir[PATH_MAX];
	char *topdirectory = "/var/spool/smsgui";
	char *groupfn = "/etc/smsgroups";
	char fn[PATH_MAX], l[1024];
	FILE *fd;
	char *smstextenv, *descrtextenv, *xmittextenv;
	char xmittext[50];
	action_t action;
	char **grouplist;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
			set_debugfile("/tmp/sms.dbg", 0);
		}
		else if (argnmatch(argv[argi], "--topdir=")) {
			char *p = strchr(argv[argi], '=');
			topdirectory = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--groupfile=")) {
			char *p = strchr(argv[argi], '=');
			groupfn = strdup(p+1);
		}

	}

	/* Determine the logged in user and switch to his/her message directory */
	realusername = username = getenv("REMOTE_USER");
	if (!username) {
		errormsg("Not logged in");
		return 0;
	}

	grouplist = getgrouplist(groupfn, username);
	action = parse_query();

	dbgprintf("selectedgroup = %s\n", (selectedgroup ? selectedgroup : "NONE"));
	if (selectedgroup) {
		/* Check the group membership before allowing the switch to a group ID */
		int ismember = 0, groupi = 0;

		dbgprintf("Checking group membership\n");
		while (!ismember && grouplist[groupi]) {
			dbgprintf("Matching selectedgroup=%s against group %s\n", selectedgroup, grouplist[groupi]);
			ismember = (strcmp(selectedgroup, grouplist[groupi]) == 0);
			groupi++;
		}
		if (ismember) username = selectedgroup;
	}
	dbgprintf("username=%s\n", username);

	snprintf(userdir, sizeof(userdir)-1, "%s/%s", topdirectory, username);
	if (chdir(userdir) != 0) mkdir(userdir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (chdir(userdir) != 0) {
		errormsg("Cannot create/access user directory\n");
		return 0;
	}

	dbgprintf("ACTION=%d\n", action);

	strcpy(msgtextenv, "INFOMSG=");
	putenv(msgtextenv);

	switch (action) {
	  case ACT_DELETE_MESSAGE:
		if (selectedmsg) {
			char activedn[PATH_MAX], olddn[PATH_MAX];
			struct stat st;

			if ((stat("old", &st) == -1) && (errno == ENOENT)) mkdir ("old", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

			init_timestamp();
			snprintf(fn, sizeof(fn), "active/%s/log", selectedmsg);
			fd = fopen(fn, "a");
			if (fd) {
				fprintf(fd, "%s : Message deleted by '%s'\n", timestamp, realusername);
				fclose(fd);
			}

			snprintf(activedn, sizeof(activedn), "active/%s", selectedmsg);
			snprintf(olddn, sizeof(olddn), "old/%s", selectedmsg);
			if (rename(activedn, olddn) == 0) {
				infomsg = NULL;
				snprintf(msgtextenv, sizeof(msgtextenv), "INFOMSG=Message deleted");
				putenv(msgtextenv);
			}
			else 
				tell_io_error("Could not delete message", activedn);
		}
		else {
			infomsg = "No message selected";
		}
		break;

	  case ACT_REACTIVATE_MESSAGE:
		{
			char oldmsgdir[PATH_MAX], newmsgdir[PATH_MAX], oldfn[PATH_MAX], newfn[PATH_MAX];
			DIR *olddir;
			struct dirent *d;
			char *newid = NULL;

			if (!selectedmsg) {
				action = ACT_EDIT_MESSAGE;
				break;
			}

			sprintf(oldmsgdir, "old/%s", selectedmsg);
			strcpy(newmsgdir, "active/smsXXXXXX");
			if (mkdtemp(newmsgdir) == NULL) infomsg = tell_io_error("Cannot create message directory", newmsgdir);
			newid = strchr(newmsgdir, '/'); if (newid) newid++;

			/* Make sure the message begins as "Suspended" */
			sprintf(newfn, "%s/repeat", newmsgdir);
			fd = fopen(newfn, "w");
			if (fd) {
				fprintf(fd, "-1");
				fclose(fd);
			}

			/* The "endtime" file must be re-created */
			sprintf(newfn, "%s/endtime", newmsgdir);
			fd = fopen(newfn, "w"); 
			if (fd) { 
				struct utimbuf ut;

				fprintf(fd, "%d\n", lifetime);
				fclose(fd); 
				ut.actime = ut.modtime = getcurrenttime(NULL) + 60*lifetime; 
				utime(newfn, &ut);
			} 
			else 
				infomsg = tell_io_error("Cannot create endtime file", newfn);


			dbgprintf("Re-activating user %s message %s as %s\n", username, selectedmsg, newid);
			olddir = opendir(oldmsgdir);
			if (olddir) {
				while ((d = readdir(olddir)) != NULL) {
					if (strcmp(d->d_name, ".") == 0) continue;
					if (strcmp(d->d_name, "..") == 0) continue;
					if (strcmp(d->d_name, "repeat") == 0) continue;		/* Will be handled specially */
					if (strcmp(d->d_name, "endtime") == 0) continue;	/* Will be handled specially */

					sprintf(oldfn, "%s/%s", oldmsgdir, d->d_name);
					sprintf(newfn, "%s/%s", newmsgdir, d->d_name);
					if (rename(oldfn, newfn) != 0) tell_io_error("Could not rename file", oldfn);
				}
				closedir(olddir);
			}

			/* The old "endtime" file must be deleted */
			sprintf(oldfn, "%s/endtime", oldmsgdir);
			if (unlink(oldfn) != 0) tell_io_error("Could not delete endtime file", oldfn);

			/* Delete the (now empty) message directory */
			if (rmdir(oldmsgdir) != 0) tell_io_error("Could not delete directory", oldmsgdir);

			/* Log it */
			sprintf(newfn, "%s/log", newmsgdir);
			fd = fopen(newfn, "a");
			if (fd) {
				init_timestamp();
				fprintf(fd, "%s : Message reactivated by '%s' with new id %s\n",  timestamp, realusername, newid);
				fclose(fd);
			}

			action = ACT_EDIT_MESSAGE;
			selectedmsg = strdup(newid);
		}
		break;

	  case ACT_UPDATE_MESSAGE:
		{
			char msgdir[20];

			if (!selectedmsg) {
				/* Create a new message directory */
				strcpy(msgdir, "active/smsXXXXXX");
				if (mkdtemp(msgdir) == NULL) infomsg = tell_io_error("Cannot create message directory", msgdir);
				selectedmsg = strdup(msgdir + strlen("active/"));
			}
			else {
				snprintf(msgdir, sizeof(msgdir)-1, "active/%s", selectedmsg);
			}

			if (!infomsg) {
				sprintf(fn, "%s/message", msgdir);
				fd = fopen(fn, "w"); 
				if (fd) { fprintf(fd, "%s", smstext); fclose(fd); } else infomsg = tell_io_error("Cannot create message file", fn);
			}

			if (!infomsg) {
				sprintf(fn, "%s/recips", msgdir);
				fd = fopen(fn, "w"); 
				if (fd) { fprintf(fd, "%s", recipientlistname); fclose(fd); } else infomsg = tell_io_error("Cannot create recips file", fn);
			}

			if (!infomsg) {
				sprintf(fn, "%s/repeat", msgdir);
				/* 
				 * We must check if the repeat interval changes from a "Suspended" state to an active one.
				 * If it does, then flag the message for immediate transmission.
				 */
				fd = fopen(fn, "r"); 
				if (fd) {
					char buf[100];
					int oldrepeatsetting = 30;

					if (fgets(buf, sizeof(buf), fd)) oldrepeatsetting = atoi(buf);
					fclose(fd);
					if ((oldrepeatsetting <= 0) && (repeatinterval > 0)) {
						sendimmediately = 1;
					}
				}
				fd = fopen(fn, "w"); 
				if (fd) { fprintf(fd, "%d", repeatinterval); fclose(fd); } else infomsg = tell_io_error("Cannot create repeat file", fn);
			}

			if (!infomsg) {
				sprintf(fn, "%s/info", msgdir);
				fd = fopen(fn, "w"); 
				if (fd) { fprintf(fd, "%s", descrtext); fclose(fd); } else infomsg = tell_io_error("Cannot create info file", fn);
			}

			if (!infomsg) {
				sprintf(fn, "%s/log", msgdir);
				fd = fopen(fn, "a"); 
				if (fd) { 
					init_timestamp();
					fprintf(fd, "%s : Message updated by '%s': Recipients='%s', repeat=%d, text='%s'\n",  
						timestamp, realusername, recipientlistname, repeatinterval, smstext);
					fclose(fd);
				} else infomsg = tell_io_error("Cannot create log file", fn);
			}

			if (!infomsg) {
				struct stat st;
				struct utimbuf ut;

				sprintf(fn, "%s/endtime", msgdir);
				if (stat(fn, &st) == 0) {
					/* File exists, so we're updating an active entry. Dont change endtime */
				}
				else {
					/* New entry - create endtime */
					fd = fopen(fn, "w"); 
					if (fd) { 
						fprintf(fd, "%d\n", lifetime);
						fclose(fd); 
						ut.actime = ut.modtime = getcurrenttime(NULL) + 60*lifetime; 
						utime(fn, &ut);
					} 
					else 
						infomsg = tell_io_error("Cannot create endtime file", fn);
				}
			}

			if (!infomsg) {
				struct stat st;
				struct utimbuf ut;

				sprintf(fn, "%s/lastxmit", msgdir);
				if ((stat(fn, &st) == 0) && (!sendimmediately)) {
					/* File exists, so we're updating an active entry. Dont change lastxmit */
				}
				else {
					/* New entry - create lastxmit and make sure it starts sending immediately */
					fd = fopen(fn, "w"); 
					if (fd) { 
						fclose(fd); 
						ut.actime = ut.modtime = 0; 
						utime(fn, &ut);
					} 
					else
						infomsg = tell_io_error("Cannot create lastxmit file", fn);
				}
			}

			snprintf(msgtextenv, sizeof(msgtextenv), "INFOMSG=Message '%s' updated", descrtext);
			putenv(msgtextenv);
		}
		break;

	  case ACT_EDIT_MESSAGE:
		/* No specific action */
		break;


	  case ACT_DELETE_RECIPLIST:
		if (recipientlistname) {
			snprintf(fn, sizeof(fn), "recips/%s", recipientlistname);
			remove(fn);
			snprintf(msgtextenv, sizeof(msgtextenv), "INFOMSG=List '%s' deleted", recipientlistname);
			putenv(msgtextenv);
		}
		break;

	  case ACT_UPDATE_RECIPLIST:
		if (!recipientlistname) recipientlistname = descrtext;

		if (recipientlistname && (*recipientlistname != '<')) {
			int i;

			snprintf(fn, sizeof(fn), "recips/%s", recipientlistname);
			fd = fopen(fn, "w");
			if (fd) {
				for (i = 0; (i < (sizeof(recipnames) / sizeof(recipnames[0]))); i++) {
					if (recipnames[i] && recipnumbers[i]) fprintf(fd, "%s %s\n", recipnumbers[i], recipnames[i]);
				}
				fclose(fd);
				snprintf(msgtextenv, sizeof(msgtextenv), "INFOMSG=List '%s' updated", recipientlistname);
				putenv(msgtextenv);
			}
			else tell_io_error("Cannot update recipient list", fn);
		}
		break;

	  case ACT_EDIT_RECIPLIST:
		/* No specific action */
		break;

	  case ACT_VIEW_FINISHED:
	  case ACT_VIEW_SUMMARY:
	  	break;

	  default:
		break;
	}

	switch (action) {
	  case ACT_DELETE_MESSAGE:
	  case ACT_UPDATE_MESSAGE:
	  case ACT_EDIT_MESSAGE:
		fd = NULL;

		if (selectedmsg) {
			snprintf(fn, sizeof(fn)-1, "active/%s/info", selectedmsg);
			fd = fopen(fn, "r");
		}
		if (fd) {
			fgets(l, sizeof(l), fd);
			fclose(fd);
			descrtext = strdup(l);
		}
		else {
			descrtext = "<Enter description>";
		}
		descrtextenv = (char *)malloc(9 + strlen(descrtext) + 1);
		sprintf(descrtextenv, "DESCRTXT=%s", descrtext);
		putenv(descrtextenv);

		if (selectedmsg) {
			snprintf(fn, sizeof(fn)-1, "active/%s/recips", selectedmsg);
			fd = fopen(fn, "r");
		}
		if (fd) {
			fgets(l, sizeof(l), fd);
			fclose(fd);
			recipientlistname = strdup(l);
		}
		else {
			recipientlistname = NULL;
		}

		if (selectedmsg) {
			snprintf(fn, sizeof(fn)-1, "active/%s/repeat", selectedmsg);
			fd = fopen(fn, "r");
		}
		if (fd) {
			fgets(l, sizeof(l), fd);
			fclose(fd);
			repeatinterval = atoi(l);
		}
		else {
			repeatinterval = 30;
		}

		if (selectedmsg) {
			snprintf(fn, sizeof(fn)-1, "active/%s/endtime", selectedmsg);
			fd = fopen(fn, "r");
		}
		if (fd) {
			fgets(l, sizeof(l), fd);
			fclose(fd);
			lifetime = atoi(l);
		}
		else {
			lifetime = DEFAULT_LIFETIME;
		}

		if (selectedmsg) {
			snprintf(fn, sizeof(fn)-1, "active/%s/message", selectedmsg);
			fd = fopen(fn, "r");
		}
		if (fd) {
			struct stat st;
			int n;

			if (stat(fn, &st) == 0) {
				smstext = (char *)malloc(st.st_size + 1);
				n = fread(smstext, 1, st.st_size, fd);
				smstext[(n >= 0) ? n : 0] = '\0';
			}
			else {
				fgets(l, sizeof(l), fd);
				smstext = strdup(l);
			}
			fclose(fd);
		}
		else {
			smstext = "<Enter text>";
		}
		smstextenv = (char *)malloc(8 + strlen(smstext) + 1);
		sprintf(smstextenv, "SMSTEXT=%s", smstext);
		putenv(smstextenv);

		strcpy(xmittext, "No messages sent yet");
		if (selectedmsg) {
			struct stat st;

			snprintf(fn, sizeof(fn)-1, "active/%s/lastxmit", selectedmsg);
			if ((stat(fn, &st) == 0) && (st.st_mtime > 0)) {
				strftime(xmittext, sizeof(xmittext), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
			}
		}
		xmittextenv = (char *)malloc(9 + strlen(xmittext) + 1);
		sprintf(xmittextenv, "XMITTEXT=%s", xmittext);
		putenv(xmittextenv);

		setup_smsgroup_selection(grouplist);
		setup_recipientlist_selection("_RECIPS", 0);
		setup_smslist_selection();
		setup_repeat_selection();
		setup_lifetime_selection(0);

		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, hffile, "sms_form", COL_BLUE, getcurrenttime(NULL), infomsg, NULL);
		break;

	  case ACT_VIEWLOG_MESSAGE:
		fd = NULL;

		fprintf(stdout, "Content-type: %s\n\n", "text/plain");
		if (selectedmsg) {
			sprintf(fn, "%s/%s/log", selectedqueue, selectedmsg);
			fd = fopen(fn, "r");
		}
		if (!fd) {
			fprintf(stdout, "No message selected, or logfile is inaccessible\n");
		}
		else {
			char buf[4096];
			int n;

			while ((n = fread(buf, 1, sizeof(buf), fd)) > 0) fwrite(buf, 1, n, stdout);
			fclose(fd);
		}
		break;

	  case ACT_DELETE_RECIPLIST:
		recipientlistname = NULL;
	  case ACT_UPDATE_RECIPLIST:
	  case ACT_EDIT_RECIPLIST:
		if (recipientlistname) {
			descrtext = recipientlistname;
		}
		else {
			descrtext = "<Enter name>";
		}
		descrtextenv = (char *)malloc(9 + strlen(descrtext) + 1);
		sprintf(descrtextenv, "DESCRTXT=%s", descrtext);
		putenv(descrtextenv);

		{
			char l[1024];
			int i, lsize=0;
			char s[1024], *envtext;
			int rseq[MAX_RECIPIENTS];

			if (recipientlistname) {
				memset(recipnames, 0, sizeof(recipnames));
				memset(recipnumbers, 0, sizeof(recipnumbers));
				sprintf(fn, "recips/%s", recipientlistname);
				fd = fopen(fn, "r"); i = 0;
				while (fd && (i < (sizeof(recipnames) / sizeof(recipnames[0])) && (fgets(l, sizeof(l), fd) != NULL))) {
					char *tok;

					tok = strtok(l, " \t\n");
					recipnumbers[i] = strdup(tok);
					tok = strtok(NULL, "\n");
					recipnames[i] = strdup(tok ? tok : "");

					i++; lsize++;
				}
				if (fd) fclose(fd);
			}
			else {
				for (i = 0; (i < (sizeof(recipnames) / sizeof(recipnames[0]))); i++) {
					recipnames[i] = recipnumbers[i] = NULL;
				}
			}

			/*
			 * Sort the recipient list, but we must shuffle both the names and the numbers.
			 * So we use a temporary sequence-table.
			 */
			for (i=0; (i < lsize); i++) rseq[i] = i;
			for (i=lsize; (i < (sizeof(recipnames) / sizeof(recipnames[0]))); i++) rseq[i] = -1;
			qsort(&rseq[0], lsize, sizeof(int), rseqcompare);

			for (i = 0; (i < (sizeof(recipnames) / sizeof(recipnames[0]))); i++) {
				sprintf(s, "RECIPNAME_%d=%s", i, ((rseq[i] >= 0) ? recipnames[rseq[i]] : ""));
				putenv(strdup(s));
				sprintf(s, "RECIPNUMBER_%d=%s", i, ((rseq[i] >= 0) ? recipnumbers[rseq[i]] : ""));
				putenv(strdup(s));
			}
		}

		{
			char *grouplistenv = (char *)malloc(11+strlen(username));
			sprintf(grouplistenv, "GROUPNAME=%s", username);
			putenv(grouplistenv);
		}

		{
			char *grouplistreadonlyenv = (char *)malloc(25);
			strcpy(grouplistreadonlyenv, "RODESCRIPTION=");
			if (recipnames[0]) strcat(grouplistreadonlyenv, "READONLY");
			putenv(grouplistreadonlyenv);
		}

		setup_recipientlist_selection("_RECIPCHOICES", 1);

		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, hffile, "sms_reciplist_form", COL_BLUE, getcurrenttime(NULL), infomsg, NULL);
		break;

	  case ACT_VIEW_FINISHED:
		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		headfoot(stdout, "sms_summary", "", "header", COL_BLUE);
		show_summary(topdirectory, "old", stdout, grouplist);
		headfoot(stdout, hffile, "", "footer", COL_BLUE);
	  	break;

	  case ACT_VIEW_SUMMARY:
		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		headfoot(stdout, "sms_summary", "", "header", COL_BLUE);
		show_summary(topdirectory, "active", stdout, grouplist);
		headfoot(stdout, hffile, "", "footer", COL_BLUE);
	  	break;

	  default:
		break;
	}

	return 0;
}


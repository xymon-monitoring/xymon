#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	char buf[10240];

	while (fgets(buf, sizeof(buf), stdin)) {
		char *dlim;

		dlim = (*buf == '[') ? strchr(buf, ']') : NULL;
		if (dlim != NULL) {
			*dlim = '\0';
			if      (strcmp(buf+1, "hobbitd") == 0) 	printf("[xymond]%s", dlim+1);
			else if (strcmp(buf+1, "bbstatus") == 0)	printf("[storestatus]%s", dlim+1);
			else if (strcmp(buf+1, "bbhistory") == 0)	printf("[history]%s", dlim+1);
			else if (strcmp(buf+1, "hostdata") == 0)	printf("[hostdata]%s", dlim+1);
			else if (strcmp(buf+1, "bbdata") == 0)		printf("[storedata]%s", dlim+1);
			else if (strcmp(buf+1, "bbnotes") == 0)		printf("[storenotes]%s", dlim+1);
			else if (strcmp(buf+1, "bbenadis") == 0)	printf("[storeenadis]%s", dlim+1);
			else if (strcmp(buf+1, "bbpage") == 0)		printf("[alert]%s", dlim+1);
			else if (strcmp(buf+1, "rrdstatus") == 0)	printf("[rrdstatus]%s", dlim+1);
			else if (strcmp(buf+1, "larrdstatus") == 0)	printf("[rrdstatus]%s", dlim+1);
			else if (strcmp(buf+1, "rrddata") == 0)		printf("[rrddata]%s", dlim+1);
			else if (strcmp(buf+1, "larrddata") == 0)	printf("[rrddata]%s", dlim+1);
			else if (strcmp(buf+1, "clientdata") == 0)	printf("[clientdata]%s", dlim+1);
			else if (strcmp(buf+1, "bbproxy") == 0)		printf("[xymonproxy]%s", dlim+1);
			else if (strcmp(buf+1, "hobbitfetch") == 0)	printf("[xymonfetch]%s", dlim+1);
			else if (strcmp(buf+1, "bbdisplay") == 0)	printf("[xymongen]%s", dlim+1);
			else if (strcmp(buf+1, "bbcombotest") == 0)	printf("[combostatus]%s", dlim+1);
			else if (strcmp(buf+1, "bbnet") == 0)		printf("[xymonnet]%s", dlim+1);
			else if (strcmp(buf+1, "bbretest") == 0)	printf("[xymonnetagain]%s", dlim+1);
			else if (strcmp(buf+1, "hobbitclient") == 0)	printf("[xymonclient]%s", dlim+1);
			else {
				*dlim = ']';
				printf("%s", buf);
			}

			continue;
		}

		dlim = buf + strspn(buf, " \t");
		if (strncasecmp(dlim, "NEEDS", 5) == 0) {
			char *nam;
			char savchar;

			nam = dlim + 5; nam += strspn(nam, " \t");
			dlim = nam + strspn(nam, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefhijklmnopqrstuvwxyz");
			savchar = *dlim; *dlim = '\0';

			if      (strcmp(nam, "hobbitd") == 0) 		{ *nam = '\0'; printf("%sxymond%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbstatus") == 0)		{ *nam = '\0'; printf("%sstorestatus%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbhistory") == 0)		{ *nam = '\0'; printf("%shistory%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "hostdata") == 0)		{ *nam = '\0'; printf("%shostdata%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbdata") == 0)		{ *nam = '\0'; printf("%sstoredata%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbnotes") == 0)		{ *nam = '\0'; printf("%sstorenotes%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbenadis") == 0)		{ *nam = '\0'; printf("%sstoreenadis%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbpage") == 0)		{ *nam = '\0'; printf("%salert%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "rrdstatus") == 0)		{ *nam = '\0'; printf("%srrdstatus%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "larrdstatus") == 0)	{ *nam = '\0'; printf("%srrdstatus%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "rrddata") == 0)		{ *nam = '\0'; printf("%srrddata%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "larrddata") == 0)		{ *nam = '\0'; printf("%srrddata%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "clientdata") == 0)	{ *nam = '\0'; printf("%sclientdata%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbproxy") == 0)		{ *nam = '\0'; printf("%sxymonproxy%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "hobbitfetch") == 0)	{ *nam = '\0'; printf("%sxymonfetch%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbdisplay") == 0)		{ *nam = '\0'; printf("%sxymongen%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbcombotest") == 0)	{ *nam = '\0'; printf("%scombostatus%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbnet") == 0)		{ *nam = '\0'; printf("%sxymonnet%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "bbretest") == 0)		{ *nam = '\0'; printf("%sxymonnetagain%c%s", buf, savchar, dlim); }
			else if (strcmp(nam, "hobbitclient") == 0)	{ *nam = '\0'; printf("%sxymonclient%c%s", buf, savchar, dlim); }
			else {
				*dlim = savchar;
				printf("%s", buf);
			}

			continue;
		}

		printf("%s", buf);
	}

	return 0;
}


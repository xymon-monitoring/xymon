/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for MQ collector                                     */
/*                                                                            */
/* Copyright (C) 2009 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char mqcollect_rcsid[] = "$Id$";

void mqcollect_flush_status(int color, char *fromline, time_t timestamp, 
			    char *hostname, char *qmid,
			    strbuffer_t *redsummary, strbuffer_t *yellowsummary, strbuffer_t *greensummary,
			    char *clienttext)
{
	char *groups;
	char msgline[1024];

	/* Generate the status message */
	groups = getalertgroups();
	init_status(color);
	if (groups) sprintf(msgline, "status/group:%s ", groups); else strcpy(msgline, "status ");
	addtostatus(msgline);
	sprintf(msgline, "%s.mq %s %s\n", 
		hostname, 
		colorname(color), ctime(&timestamp));
	addtostatus(msgline);
	if (STRBUFLEN(redsummary) > 0) {
		addtostrstatus(redsummary);
		addtostatus("\n");
	}
	if (STRBUFLEN(yellowsummary) > 0) {
		addtostrstatus(yellowsummary);
		addtostatus("\n");
	}
	if (STRBUFLEN(greensummary) > 0) {
		addtostrstatus(greensummary);
		addtostatus("\n");
	}
	addtostatus(clienttext);
	addtostatus("\n");
	addtostatus(fromline);
	finish_status();
}

void handle_mqcollect_client(char *hostname, char *clienttype, enum ostype_t os, 
				void *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	char *qmline = "Starting MQSC for queue manager ";
	strbuffer_t *redsummary = newstrbuffer(0);
	strbuffer_t *yellowsummary = newstrbuffer(0);
	strbuffer_t *greensummary = newstrbuffer(0);
	char *chngroup, *bol, *eoln, *clienttext;
	int color = COL_GREEN;
	char fromline[1024], msgline[1024];
	char *qmid = NULL, *qnam = NULL; int qlen = -1, qage = -1; char *chnnam = NULL, *chnstatus = NULL;
	enum { PARSING_QL, PARSING_QS, PARSING_CHS, PARSER_FLOAT } pstate = PARSER_FLOAT;
	int lastline = 0;

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	bol = strchr(clientdata, '\n'); if (bol) bol++; clienttext = bol;
	while (bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0'; else lastline = 1;
		bol += strspn(bol, " \t");

		if (strncmp(bol, qmline, strlen(qmline)) == 0) {
			char *p;

			if (qmid) xfree(qmid);
			qmid = strdup(bol+strlen(qmline));
			p = strrchr(qmid, '.'); if (p) *p = '\0';
		}

		else if ( (strncmp(bol, "AMQ8409:", 8) == 0) ||		/* "ql" command - Queue details, incl. depth */
		          (strncmp(bol, "AMQ8450:", 8) == 0) ||		/* "qs" command - Queue status */
		          (strncmp(bol, "AMQ8417:", 8) == 0) ||		/* "chs" command - Channel status */
			  lastline                              ) {

			if ( ((pstate == PARSING_QL) || (pstate == PARSING_QS)) && qmid && qnam && (qlen >= 0)) {
				/* Got a full queue depth status */
				int warnlen, critlen, warnage, critage;
				char *trackid;
				get_mqqueue_thresholds(hinfo, clienttype, qmid, qnam, &warnlen, &critlen, &warnage, &critage, &trackid);

				if ((critlen != -1) && (qlen >= critlen)) {
					color = COL_RED;
					sprintf(msgline, "&red Queue %s:%s has depth %d (critical: %d, warn: %d)\n",
						qmid, qnam, qlen, critlen, warnlen);
					addtobuffer(redsummary, msgline);
				}
				else if ((warnlen != -1) && (qlen >= warnlen)) {
					if (color < COL_YELLOW) color = COL_YELLOW;
					sprintf(msgline, "&yellow Queue %s:%s has depth %d (warn: %d, critical: %d)\n",
						qmid, qnam, qlen, warnlen, critlen);
					addtobuffer(yellowsummary, msgline);
				}
				else if ((warnlen != -1) || (critlen != -1)) {
					sprintf(msgline, "&green Queue %s:%s has depth %d (warn: %d, critical: %d)\n",
						qmid, qnam, qlen, warnlen, critlen);
					addtobuffer(greensummary, msgline);
				}

				if ((pstate == PARSING_QS) && (qage >= 0)) {
					if ((critage != -1) && (qage >= critage)) {
						color = COL_RED;
						sprintf(msgline, "&red Queue %s:%s has age %d (critical: %d, warn: %d)\n",
								qmid, qnam, qage, critage, warnage);
						addtobuffer(redsummary, msgline);
					}
					else if ((warnage != -1) && (qage >= warnage)) {
						if (color < COL_YELLOW) color = COL_YELLOW;
						sprintf(msgline, "&yellow Queue %s:%s has age %d (warn: %d, critical: %d)\n",
								qmid, qnam, qage, warnage, critage);
						addtobuffer(yellowsummary, msgline);
					}
					else if ((warnage != -1) || (critage != -1)) {
						sprintf(msgline, "&green Queue %s:%s has age %d (warn: %d, critical: %d)\n",
								qmid, qnam, qage, warnage, critage);
						addtobuffer(greensummary, msgline);
					}
				}

				if (trackid) {
					/* FIXME: Send "data" message for creating queue-length RRD */
				}

				pstate = PARSER_FLOAT;
			}

			if ((pstate == PARSING_CHS) && qmid && chnnam && chnstatus) {
				/* Got a full channel status */
				int chncolor;

				if (get_mqchannel_params(hinfo, clienttype, qmid, chnnam, chnstatus, &chncolor)) {
					if (chncolor > color) color = chncolor;
					switch (chncolor) {
						case COL_RED:
							sprintf(msgline, "&red Channel %s:%s has status %s\n", qmid, chnnam, chnstatus);
							addtobuffer(redsummary, msgline);
							break;
						case COL_YELLOW:
							sprintf(msgline, "&yellow Channel %s:%s has status %s\n", qmid, chnnam, chnstatus);
							addtobuffer(yellowsummary, msgline);
							break;
						case COL_GREEN:
							sprintf(msgline, "&green Channel %s:%s has status %s\n", qmid, chnnam, chnstatus);
							addtobuffer(greensummary, msgline);
							break;
					}
				}

				pstate = PARSER_FLOAT;
			}

			if (qnam) xfree(qnam);
			qlen = qage = -1;
			if (chnnam) xfree(chnnam);
			if (chnstatus) xfree(chnstatus);

			if (strncmp(bol, "AMQ8409:", 8) == 0) pstate = PARSING_QL;
			else if (strncmp(bol, "AMQ8450:", 8) == 0) pstate = PARSING_QS;
			else if (strncmp(bol, "AMQ8417:", 8) == 0) pstate = PARSING_CHS;
			else pstate = PARSER_FLOAT;
		}
		else if ((pstate == PARSING_QL) || (pstate == PARSING_QS)) {
			char *bdup = strdup(bol);
			char *tok = strtok(bdup, " \t");
			while (tok) {
				if (strncmp(tok, "QUEUE(", 6) == 0) {
					char *p;

					qnam = strdup(tok+6);
					p = strchr(qnam, ')');
					if (p) *p = '\0';
				}
				else if (strncmp(tok, "CURDEPTH(", 9) == 0) {
					qlen = atoi(tok+9);
				}
				else if (strncmp(tok, "MSGAGE(", 7) == 0) {
					if (isdigit(*(tok+7))) qage = atoi(tok+7);
				}

				tok = strtok(NULL, " \t");
			}

			xfree(bdup);
		}
		else if (pstate == PARSING_CHS) {
			char *bdup = strdup(bol);
			char *tok = strtok(bdup, " \t");
			while (tok) {
				if (strncmp(tok, "CHANNEL(", 8) == 0) {
					char *p;

					chnnam = strdup(tok+8);
					p = strchr(chnnam, ')');
					if (p) *p = '\0';
				}
				else if (strncmp(tok, "STATUS(", 7) == 0) {
					char *p;

					chnstatus = strdup(tok+7);
					p = strchr(chnstatus, ')');
					if (p) *p = '\0';
				}

				tok = strtok(NULL, " \t");
			}
			xfree(bdup);
		}

		if (eoln) {
			*eoln = '\n';
			bol = eoln+1;
		}
		else
			bol = NULL;
	}

	mqcollect_flush_status(color, fromline, timestamp, 
			       hostname, qmid,
			       redsummary, yellowsummary, greensummary,
			       clienttext);

	if (qmid) xfree(qmid);
	freestrbuffer(greensummary);
	freestrbuffer(yellowsummary);
	freestrbuffer(redsummary);
}


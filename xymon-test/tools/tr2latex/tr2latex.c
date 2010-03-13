/*
** tr2latex - troff to LaTeX converter
** COPYRIGHT (C) 1987 Kamal Al-Yahya, 1991,1992 Christian Engel
**
** Module: tr2latex.c
**
** This module contains the main function inititating the translation
** and supporting the Usage.
*/

#include <getopt.h>

#define MAIN
#include	"setups.h"
#include	"protos.h"

const char *version = "2.3";

int	man,		/* option -m: manual page */
  fontsize,	/* option -9/-10/-11/-12: font size */
  twoside,	/* option -t: twoside */
  piped_in;

char *document = "article";	/* document type, see also -s option */

FILE *out_file;		/* in case they can't use UN*X redirecting or piping */

char *prgname;
char inbuf[MAXLEN],
  outbuf[MAXLEN];

static void usage (int exitcode)
{
  printf ("tr2latex (c) 1986/1987 Kamal Al-Yahya, 1991 C. Engel, 2008..2009 Dirk Jagdmann\n"
	  "Version %s\n",
	  version);
  printf ("tr2latex - troff to LaTeX converter\n"
	  "SYNTAX:  tr2latex [-m] [-t] [-<n>] [-s <style>] [-o <outfile>] [-d] [-b] [<file>...]\n"
	  "options: -m            for manual\n"
	  "         -t            twoside page style\n"
	  "         -<n>          a number n gives the font size (default is 12pt\n"
	  "                       for man, 11pt otherwise)\n"
	  "         -s <style>    use documentstyle <style> (default is article)\n"
	  "         -o <outfile>  send output to <outfile> (default is stdout)\n"
	  "         -d            debug output\n"
	  "         -b            LaTeX body, don't print declarations\n"
	  );

  exit (exitcode);
}

static void process (FILE *in_file, char *f_name, char *pin, char *pout)
{
  static char sep[] = "--------------------------------------------------";

  tmpbuf (in_file, pin);
  fprintf (out_file, "%%%s\n%% start of input file: %s\n%%\n", sep, f_name);
  troff_tex (pin, pout, 0, 0);
  fputs (pout, out_file);
  fprintf (out_file, "%%\n%% end of input file: %s\n%%%s\n", f_name, sep);
}

int main (int argc, char *argv[])
{
  char *pin = inbuf,
    *pout = outbuf;
  FILE *in_file;
  long timeval;		/* clock value from time() for ctime()	*/
  int c, only_body=0;

  prgname = argv [0];
  out_file = stdout;		/* default output */

  /* process option flags */
#if 0
  getopts (&argc, argv);
#else

  while((c=getopt(argc, argv, "0123456789bhmtds:o:")) != EOF)
    switch(c)
      {
      default:
      case '?':
      case 'h': usage (EXIT_SUCCESS);

      case 'b': only_body=1; break;
      case 'm': man = 1; break;
      case 't': twoside = 1; break;
      case 's': document = optarg; break;
      case 'o':
	if ((out_file = fopen(optarg, "w")) == NULL)
	  {
	    fprintf(stderr, "%s: can't open %s\n", prgname, optarg);
	    usage (EXIT_FAILURE);
	  }
	break;
      case 'd': debug_o = 1; break;

      case '0': fontsize = 10; break;
      case '1': fontsize = 11; break;
      case '2': fontsize = 12; break;
      case '3': fontsize = 13; break;
      case '4': fontsize = 14; break;
      case '5': fontsize = 15; break;
      case '6': fontsize = 16; break;
      case '7': fontsize = 17; break;
      case '8': fontsize = 18; break;
      case '9': fontsize = 9; break;
      }
#endif

  /* initialize spacing and indentation parameters */
  strcpy(linespacing.def_units,"\\normalbaselineskip");
  strcpy(linespacing.old_units,"\\normalbaselineskip");
  strcpy(indent.def_units,"em");
  strcpy(indent.old_units,"em");
  strcpy(tmpind.def_units,"em");
  strcpy(tmpind.old_units,"em");
  strcpy(space.def_units,"\\baselineskip");
  strcpy(space.old_units,"\\baselineskip");
  strcpy(vspace.def_units,"pt");
  strcpy(vspace.old_units,"pt");
  linespacing.value = 1.;
  linespacing.old_value = 1.;
  indent.value = 0.;
  indent.old_value = 0.;
  tmpind.value = 0.;
  tmpind.old_value = 0.;
  space.value = 1.;
  space.old_value = 1.;
  vspace.value = 1.;
  vspace.old_value = 1.;
  linespacing.def_value = 0;
  indent.def_value = 0;
  tmpind.def_value = 0;
  space.def_value = 1;
  vspace.def_value = 1;

  math_mode = 0;					/* start with non-math mode */
  de_arg = 0;                     /* not a .de argument */

  /* start of translated document */

  timeval = time(0);
  fprintf (out_file,
	   "%% -*-LaTeX-*-\n\
%% Converted automatically from troff to LaTeX\n\
%% by tr2latex %s\n\
%% on %s\
%% tr2latex was written by Kamal Al-Yahya at Stanford University <Kamal%%Hanauma@SU-SCORE.ARPA>\n\
%% and substantially enhanced by Christian Engel at RWTH Aachen <krischan@informatik.rwth-aachen.de>\n\
%%\n\
%% troff input file%s:%s",
	   version, ctime(&timeval),
	   argc>2?"s":"",
	   argc==1?" <stdin>":"");
#if 0
  for (argi = 1; argi < argc; argi++)
    {
      if (strcmp (argv [argi], "-") == 0)
	fprintf (out_file, " <stdin>");
      else
	fprintf (out_file, " %s", argv[argi]);
    }
#endif

  if(!only_body)
    {
      /* document style and options */
      fprintf (out_file,"\n\n\\documentclass[");
      if (fontsize == 0 && !man)
	fontsize = 11;
      if (fontsize != 0)
	fprintf (out_file,"%dpt", fontsize);
      if (twoside)
	fputs (",twoside", out_file);
      fprintf (out_file,"]{%s}", document);
      fprintf (out_file,"\n\n\\usepackage{%s}", man? "troffman": "troffms");
      fprintf (out_file,"\n\n\\begin{document}\n");
    }

  if (optind == argc)
    process (stdin, "<stdin>", pin, pout);
  else
    {
      for (; optind!=argc; ++optind)
	{
	  if (strcmp (argv[optind], "-") == 0)
	    process (stdin, "<stdin>", pin, pout);
	  else if ((in_file = fopen(argv[optind],"r")) == NULL)
	    fprintf(stderr,"%s: Cannot open input file `%s'\n",
		    prgname,argv[optind]);
	  else
	    {
	      process (in_file, argv[optind], pin, pout);
	      fclose(in_file);
	    }
	}
    }

  if(!only_body)
    {
      /* close translated document */
      fputs("\\end{document}\n",out_file);
    }

  exit(EXIT_SUCCESS);
}

void errexit (int exitcode)
{
  fprintf (stderr, "%s: Error #%03d ", prgname, exitcode);
  exit (exitcode);
}

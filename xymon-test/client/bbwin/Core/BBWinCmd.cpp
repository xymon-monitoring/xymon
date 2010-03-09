//this file is part of BBWin
//Copyright (C)2006-2008 Etienne GRIGNON  ( etienne.grignon@gmail.com )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// Other Credits :
// UploadMessage function written by Stef Coene <stef.coene@docum.org>
//
// $Id: BBWinCmd.cpp 90 2008-02-27 10:19:35Z sharpyy $

#include <windows.h>

#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
using namespace std;

#include "BBWinNet.h"

#define BB_DISP_LEN		512

#include "version.h"

typedef struct 	messageTable_s {
	string		argument;
	int			minArg;
	void		(*callBack)(int , char **, BBWinNet & );
} 				messageTable_t;



void 	Status(int argc, char *argv[], BBWinNet & bbobj);
void 	Data(int argc, char *argv[], BBWinNet & bbobj);
void 	Notify(int argc, char *argv[], BBWinNet & bbobj);
void 	Enable(int argc, char *argv[], BBWinNet & bbobj);
void 	Disable(int argc, char *argv[], BBWinNet & bbobj);
void 	Query(int argc, char *argv[], BBWinNet & bbobj);
void 	Config(int argc, char *argv[], BBWinNet & bbobj);
void 	Message(int argc, char *argv[], BBWinNet & bbobj);
void 	Drop(int argc, char *argv[], BBWinNet & bbobj);
void 	Rename(int argc, char *argv[], BBWinNet & bbobj);
void	Download(int argc, char *argv[], BBWinNet & bbobj);
void	UploadMessage(int argc, char *argv[], BBWinNet &bbobj);

static messageTable_t messTable[] = 
{
	{"status", 7, Status},
	{"data", 6, Data},
	{"notify", 6, Notify},
	{"enable", 5, Enable},
	{"disable", 7, Disable},
	{"query", 5, Query},
	{"config", 4, Config},
	{"uploadmessage", 4, UploadMessage},
	{"message", 4, Message}, 
	{"drop", 4, Drop},
	{"rename", 4, Rename},
	{"download", 4, Download},
	{"", NULL}
};

void 	Status(int argc, char *argv[], BBWinNet & bbobj)
{
	
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	cout << "Sending status ...\n";
	try {
		if (argc > 7)
			bbobj.Status(argv[4], argv[5], argv[6], argv[7]);
		else
			bbobj.Status(argv[4], argv[5], argv[6]);
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Data(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending data ...\n";
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	try {
		bbobj.Data(argv[4], argv[5]);
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Notify(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending notify ...\n";
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	try {
		bbobj.Notify(argv[4], argv[5]);
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Enable(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending enable ...\n";
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	try {
		bbobj.Enable(argv[4]);
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Disable(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending disable ...\n";
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	try {
		bbobj.Disable(argv[4], argv[5], argv[6]);
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Query(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending query ...\n";
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	try {
		string res;
		
		bbobj.Query(argv[4], res);
		cout << "\n" << res << "\n\n";
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Config(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending config ...\n";
	try {
		string res;
		
		res = argv[3];
		if (argc > 4)
			res = argv[4];
		bbobj.Config(argv[3], res);
		cout << "\nDownloaded file has been stored to " << res << "\n\n";
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Download(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending download ...\n";
	try {
		string res;
		
		res = argv[3];
		if (argc > 4)
			res = argv[4];
		bbobj.Download(argv[3], res);
		cout << "\nDownloaded file has been stored to " << res << "\n\n";
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Message(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Sending message ...\n";
	try {
		string res;
		
		bbobj.Message(argv[3], res);
		cout << "\n" << res << "\n\n";
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	UploadMessage(int argc, char *argv[], BBWinNet & bbobj)
{
	cout << "Uploading message ...\n";
	try {
		string res;
		ostringstream 	tosend;
        string line;

		ifstream myfile ( argv[3] );

		if (myfile.is_open())
		{
			while (! myfile.eof() )
			{
				getline (myfile,line);
				tosend << line << endl;
			}
			myfile.close();
		}
		else cout << "Unable to open file"; 

		bbobj.Message(tosend.str(), res);
		cout << "Uploading message done !" ;
		cout << "\n" << res << "\n\n";
		
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void 	Drop(int argc, char *argv[], BBWinNet & bbobj)
{
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	cout << "Sending drop ...\n";
	try {
		if (argc > 4)
			bbobj.Drop(argv[4]);
		else
			bbobj.Drop();
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}


void 	Rename(int argc, char *argv[], BBWinNet & bbobj)
{
	bbobj.SetHostName(argv[3]);
	cout << "hostname defined to: " << argv[3] << "\n";
	try {
		if (argc > 5) {
			cout << "Sending test rename ...\n";
			bbobj.Rename(argv[4], argv[5]);
		} else {
			cout << "Sending test rename ...\n";
			bbobj.Rename(argv[4]);
		}
	} catch (BBWinNetException ex) {
		cout << "Error : " << ex.getMessage() << "\n";
	}
}

void	help()
{
	cout << "\n";
	cout << "bbwincmd help : \n\n";
	cout << "bbwincmd is a command line tool to experiment the hobbit (bigbrother) protocol.\n";
	cout << "It can also be used as a diagnostic tool on computers having problems to communicate\n";
	cout << "with the hobbit server.\n";
	cout << "\n";
	cout << "usage :\n\n";
	cout << "Sending a status :\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] status <hostname> <testname> <color> <text> [<lifetime>]";
	cout << "\n\n";	
	cout << "Sending a data :\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] data <hostname> <dataname> <text>";
	cout << "\n\n";
	cout << "Sending a notify :\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] notify <hostname> <testname> <text>";
	cout << "\n\n";
	cout << "Sending a disable:\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] disable <hostname> <test> <duration> <text>";
	cout << "\n\n";
	cout << "Sending an enable:\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] enable <hostname> <test>";
	cout << "\n\n";
	cout << "Sending a query and get the result:\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] query <hostname> <test>";
	cout << "\n\n";
	cout << "Sending a config and get the file content:\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] config <filename> [<path>]";
	cout << "\n\n";
	cout << "Sending a hobbit message manually written\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] message <message>";
	cout << "\n\n";
	cout << "Sending a hobbit message manually written and stored in a file\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] uploadmessage <filename>";
	cout << "\n\n";
	cout << "Sending a drop\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] drop <hostname> [<testname>]";
	cout << "\n\n";
	cout << "Sending a hostname rename\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] rename <hostname> <newhostname>";
	cout << "\n\n";
	cout << "Sending a test rename\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] rename <hostname> <oldtestname> <newtestname>";
	cout << "\n\n";	
	cout << "Sending a download message. default download path is the filename requested itself\n\n";
	cout << "bbwincmd.exe <bbdisplay>[:<port>] download <hostname> <filename> [<path>]";
	cout << "\n\n";	
	cout << "Notes : \n\n";
	cout << "If no port is specified after bbdisplay, it will use hobbit tcp port 1984\n\n";
	cout << "You can set your default hobbit server by setting the environment variable BBDISPLAY.\n";
	cout << "To use your environment variable, replace the <bbdisplay>[:<port>] argument by the character $\n";
	cout << "\n\n";
	cout << "You are using bbwincmd v" << BBWINFILEVERSION_STR << " " << BBWINDATE_STR << ".\n";
	cout << "This tool is free and under GPL licence.\n";
	cout << "Please read the associated licence file.\n" << endl;
}


string		getBBDisplaySetting() {
	LPVOID 	env;
	string	bbdisp;
	char	buf[BB_DISP_LEN + 1];
	DWORD	res;
	
	if ((env = GetEnvironmentStrings()) == NULL) {
		return string("");
	}
	res = GetEnvironmentVariable("BBDISPLAY", buf, BB_DISP_LEN);
	if (res == 0 && res == BB_DISP_LEN) {	
		return string("");
	}
	bbdisp = buf;
	return bbdisp;
}


void		bbwincmd(int argc, char *argv[]) {
	string 	bbdisparg;
	
	BBWinNet	bbobj;
	if (strcmp(argv[1], "$") == 0) {
		cout << "will use environment variable BBDISPLAY" << endl;
		bbdisparg = getBBDisplaySetting();
	} else {
		bbdisparg = argv[1];
	}
	bbobj.SetBBDisplay(bbdisparg);
	cout << "bbdisplay defined to : " << bbobj.GetBBDisplay() << "\n";
	cout << "port defined to : " << bbobj.GetPort() << "\n";
	bbobj.SetDebug(true);
	string message = argv[2];
	bool	unkownMessage = false;
	for (int inc = 0; messTable[inc].callBack != NULL; ++inc) {
		int res = message.find(messTable[inc].argument);
		if (res >= 0 && (unsigned int)res <= message.size()) {
			unkownMessage = true;
			if (argc >= messTable[inc].minArg) {
				messTable[inc].callBack(argc, argv, bbobj);
			} else {
				cout << "\n";
				cout << "Error : not enough arguments for : \"" << message << "\"" << "\n\n";
			}
			break ;
		}
	}
	if (unkownMessage == false) {
		cout << "\n";
		cout << "Error : unkown hobbit message type : \"" << message << "\"" << "\n\n";
	}
}

VOID __cdecl main(int argc, char *argv[]) {
	if (argc < 4) {
		help();
		exit(0);
	}
	bbwincmd(argc, argv);
}

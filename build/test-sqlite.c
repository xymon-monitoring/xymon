#include <sqlite3.h>

int main(int argc, char *argv[])
{
	int dbres;
	sqlite3 *db;

	dbres = sqlite3_open("testdb.sqlite3", &db);

	return 0;
}


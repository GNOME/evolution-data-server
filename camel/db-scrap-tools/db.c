/* GPL v2 or later - Srinivasa Ragavan - sragavan@novell.com */

#include <stdio.h>
#include <sqlite3.h>

sqlite3 *db;

static gint
callback (gpointer data, gint argc, gchar **argv, gchar **azColName)
{
	gint i;
	for (i=0; i<argc; i++) {
		printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	printf("--DONE \n");

	return 0;
}

static gint
select_stmt (const gchar * stmt) {
	gchar *errmsg;
	gint   ret;
	gint   nrecs = 0;

	ret = sqlite3_exec(db, stmt, callback, &nrecs, &errmsg);

	if (ret!=SQLITE_OK) {
		printf("Error in select statement %s [%s].\n", stmt, errmsg);
	} else {
		printf("\n   %d records returned.\n", nrecs);
	}

	return ret;
}

static gint
sql_stmt(const gchar * stmt) {
	gchar *errmsg;
	gint   ret;

	ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);

	if (ret != SQLITE_OK) {
		printf("Error in statement: %s [%s].\n", stmt, errmsg);
		exit (1);
	}

	return ret;
}

#define CREATE_STMT "CREATE TABLE %s (uid TEXT PRIMARY KEY, gflags INTEGER, isize INTEGER, dsent INTEGER, dreceived INTEGER, jsubject TEXT, ffrom TEXT, tto TEXT, cc TEXT, mlist TEXT, part TEXT, userflags TEXT, usertags TEXT, bdata TEXT)"

static gint
create_table (const gchar *tablename)
{
	gchar *cmd = malloc (sizeof(CREATE_STMT)+20);
	sprintf(cmd, CREATE_STMT, tablename);
	sql_stmt (cmd);
}

gint sort_uid (gpointer foo, gint len, gpointer  data1, gint len2, gpointer data2)
{
	printf("%s \n%s\n\n", data1, data2);
	gint a1 = atoi (data1);
	gint a2 = atoi (data2);
	return a1 < a2;
}

gint sort_cmp (gpointer foo, gint len, gpointer  data1, gint len2, gpointer data2)
{
	printf("%s \n%s\n\n", data1, data2);
	gint a1 = atoi (data1);
	gint a2 = atoi (data2);
	return a1 == a2 ? 0 : a1 < a2 ? -1 : 1;
}

gint main(gint argc, gchar **argv) {
	gchar *zErrMsg = 0;
	gint rc;

//	rc = sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE , NULL);
	rc = sqlite3_open("test.db", &db);

	if (rc) {
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	}

	sqlite3_create_collation(db, "uidcmp", SQLITE_UTF8,  NULL, sort_cmp);
	sqlite3_create_collation(db, "uidsort", SQLITE_UTF8,  NULL, sort_uid);

	gchar *subject_san = "San?%*()-234@#$!@#$@#$%32424kar's Subject";
	create_table ("table1");
	sql_stmt (sqlite3_mprintf("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('5120', 100, 123, '%q', 'mlistbinary')", subject_san));
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('6103', 100, 123, 'nice subject', 'mlistbinary')");
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('3194', 100, 123, 'nice subject', 'mlistbinary')");
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('8130', 100, 123, 'nice subject', 'mlistbinary')");
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('9102', 100, 123, 'nice subject', 'mlistbinary')");
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('3112', 100, 123, 'nice subject', 'mlistbinary')");
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('1102', 100, 123, 'nice subject', 'mlistbinary')");
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('214', 100, 123, 'nice subject', 'mlistbinary')");
	sql_stmt ("INSERT INTO table1 (uid, gflags, isize, jsubject, mlist) VALUES ('3161', 0, 1123, '12nice subject', '123mlistbinary')");

//	select_stmt ("select * from table1 where uid collate uidsort order by uid collate uidsort");
	select_stmt ("select * from table1 where uid == 5120 collate uidcmp");
	printf ("\n\aFrom teh C File: [%s] \n\a", subject_san);

	printf("------\n");
	select_stmt ("select count(isize) from table1");
	sqlite3_close(db);

	return 0;
}

// openbv: the part of SQLite's C API that BV2 uses (bv2.db: LauncherSettings, MasterServers,
// BadChecksum). Implemented by src/port/sqlite_shim.cpp over the game's own bv2.db.
#ifndef OPENBV_SQLITE3_H
#define OPENBV_SQLITE3_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3 sqlite3;
typedef int (*sqlite3_callback)(void *, int, char **, char **);

#define SQLITE_OK     0
#define SQLITE_ERROR  1
#define SQLITE_CANTOPEN 14

int  sqlite3_open(const char *filename, sqlite3 **ppDb);
int  sqlite3_close(sqlite3 *db);
int  sqlite3_exec(sqlite3 *db, const char *sql, sqlite3_callback callback, void *arg, char **errmsg);
int  sqlite3_get_table(sqlite3 *db, const char *sql, char ***pazResult, int *pnRow, int *pnColumn, char **pzErrmsg);
void sqlite3_free_table(char **result);
void sqlite3_free(void *p);

#ifdef __cplusplus
}
#endif

#endif

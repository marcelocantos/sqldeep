// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// sqldeep CLI: full SQLite shell with sqldeep transpilation and XML functions.
// Build by compiling this file (which #includes shell.c) and linking with
// sqldeep.o, sqldeep_xml.o, and sqlite3.o.

#include <sqlite3.h>
#include "sqldeep.h"
#include "sqldeep_xml.h"

/* Auto-extension entry point: called on every new connection. */
static int sd_register_xml(sqlite3 *db, char **pzErrMsg,
                            const struct sqlite3_api_routines *pApi) {
    (void)pzErrMsg; (void)pApi;
    return sqldeep_register_sqlite_xml(db);
}

/* SQLITE_SHELL_INIT_PROC: called before sqlite3_initialize(). */
void sqldeep_shell_init(void) {
    sqlite3_auto_extension((void(*)(void))sd_register_xml);
    sqlite3_initialize();
}

// ── Include the SQLite shell with sqldeep hooks enabled ────────────

#define SQLDEEP_SHELL
#define SQLITE_SHELL_INIT_PROC sqldeep_shell_init
#include "shell.c"

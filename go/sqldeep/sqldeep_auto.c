// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

#include "sqldeep_auto.h"
#include "sqldeep_xml.h"
#include <sqlite3.h>

static int sqldeep_auto_init_cb(sqlite3 *db, char **err, const void *api) {
    (void)err; (void)api;
    return sqldeep_register_sqlite(db);
}

void sqldeep_enable_auto(void) {
    sqlite3_auto_extension((void(*)(void))sqldeep_auto_init_cb);
}

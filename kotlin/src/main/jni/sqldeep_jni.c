// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// JNI bridge: registers sqldeep runtime functions on a raw sqlite3* handle.
// The handle is obtained from Android's CursorWindow internals or from
// a custom SQLite build (requery/sqlite-android, etc.).
//
// Usage from Kotlin/Java:
//   SQLDeepRuntime.register(db.nativeHandle)

#include <jni.h>
#include <sqlite3.h>
#include "sqldeep_xml.h"

JNIEXPORT jint JNICALL
Java_com_marcelocantos_sqldeep_SQLDeepRuntime_nativeRegister(
    JNIEnv *env, jclass cls, jlong dbHandle) {
    (void)env; (void)cls;
    sqlite3 *db = (sqlite3 *)(intptr_t)dbHandle;
    return sqldeep_register_sqlite(db);
}

// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// JNI bridge for sqldeep: transpiler + runtime registration.
//
// Transpiler:
//   SQLDeep.transpile(input) -> String   (throws SQLDeepException on error)
//
// Runtime:
//   SQLDeepRuntime.register(dbHandle)     (registers custom SQLite functions)

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "sqldeep.h"
#include "sqldeep_xml.h"

// ── Runtime registration ──────────────────────────────────────────

JNIEXPORT jint JNICALL
Java_com_marcelocantos_sqldeep_SQLDeepRuntime_nativeRegister(
    JNIEnv *env, jclass cls, jlong dbHandle) {
    (void)env; (void)cls;
    sqlite3 *db = (sqlite3 *)(intptr_t)dbHandle;
    return sqldeep_register_sqlite(db);
}

// ── Transpiler ────────────────────────────────────────────────────

JNIEXPORT jstring JNICALL
Java_com_marcelocantos_sqldeep_SQLDeep_nativeTranspile(
    JNIEnv *env, jclass cls, jstring input) {
    (void)cls;
    const char *c_input = (*env)->GetStringUTFChars(env, input, NULL);
    if (!c_input) return NULL; // OOM — JVM already threw

    char *err_msg = NULL;
    int err_line = 0, err_col = 0;
    char *result = sqldeep_transpile(c_input, &err_msg, &err_line, &err_col);
    (*env)->ReleaseStringUTFChars(env, input, c_input);

    if (!result) {
        // Throw SQLDeep.TranspileException with msg, line, col.
        jclass exc_cls = (*env)->FindClass(env,
            "com/marcelocantos/sqldeep/SQLDeep$TranspileException");
        if (exc_cls) {
            jmethodID ctor = (*env)->GetMethodID(env, exc_cls, "<init>",
                "(Ljava/lang/String;II)V");
            if (ctor) {
                jstring j_msg = (*env)->NewStringUTF(env,
                    err_msg ? err_msg : "unknown error");
                jobject exc = (*env)->NewObject(env, exc_cls, ctor,
                    j_msg, (jint)err_line, (jint)err_col);
                if (exc) (*env)->Throw(env, (jthrowable)exc);
            }
        }
        if (err_msg) sqldeep_free(err_msg);
        return NULL;
    }

    jstring j_result = (*env)->NewStringUTF(env, result);
    sqldeep_free(result);
    return j_result;
}

// FK-guided transpile.
JNIEXPORT jstring JNICALL
Java_com_marcelocantos_sqldeep_SQLDeep_nativeTranspileFK(
    JNIEnv *env, jclass cls, jstring input,
    jobjectArray fromTables, jobjectArray toTables,
    jobjectArray fromColArrays, jobjectArray toColArrays) {
    (void)cls;
    const char *c_input = (*env)->GetStringUTFChars(env, input, NULL);
    if (!c_input) return NULL;

    int fk_count = (*env)->GetArrayLength(env, fromTables);

    // Allocate FK descriptors and column-pair storage.
    sqldeep_foreign_key *fks = calloc(fk_count, sizeof(sqldeep_foreign_key));
    sqldeep_column_pair **col_pairs = calloc(fk_count, sizeof(sqldeep_column_pair *));
    // Temporary storage for JNI string pointers to release later.
    const char **ft_strs = calloc(fk_count, sizeof(const char *));
    const char **tt_strs = calloc(fk_count, sizeof(const char *));

    // Track all column JNI strings for cleanup.
    int total_cols = 0;
    for (int i = 0; i < fk_count; i++) {
        jobjectArray fc = (jobjectArray)(*env)->GetObjectArrayElement(env, fromColArrays, i);
        total_cols += (*env)->GetArrayLength(env, fc);
    }
    const char **fc_strs = calloc(total_cols, sizeof(const char *));
    const char **tc_strs = calloc(total_cols, sizeof(const char *));
    jstring *fc_jstrs = calloc(total_cols, sizeof(jstring));
    jstring *tc_jstrs = calloc(total_cols, sizeof(jstring));
    jstring *ft_jstrs = calloc(fk_count, sizeof(jstring));
    jstring *tt_jstrs = calloc(fk_count, sizeof(jstring));

    int col_idx = 0;
    for (int i = 0; i < fk_count; i++) {
        ft_jstrs[i] = (jstring)(*env)->GetObjectArrayElement(env, fromTables, i);
        tt_jstrs[i] = (jstring)(*env)->GetObjectArrayElement(env, toTables, i);
        ft_strs[i] = (*env)->GetStringUTFChars(env, ft_jstrs[i], NULL);
        tt_strs[i] = (*env)->GetStringUTFChars(env, tt_jstrs[i], NULL);

        jobjectArray fc = (jobjectArray)(*env)->GetObjectArrayElement(env, fromColArrays, i);
        jobjectArray tc = (jobjectArray)(*env)->GetObjectArrayElement(env, toColArrays, i);
        int nc = (*env)->GetArrayLength(env, fc);

        col_pairs[i] = calloc(nc, sizeof(sqldeep_column_pair));
        for (int j = 0; j < nc; j++) {
            fc_jstrs[col_idx] = (jstring)(*env)->GetObjectArrayElement(env, fc, j);
            tc_jstrs[col_idx] = (jstring)(*env)->GetObjectArrayElement(env, tc, j);
            fc_strs[col_idx] = (*env)->GetStringUTFChars(env, fc_jstrs[col_idx], NULL);
            tc_strs[col_idx] = (*env)->GetStringUTFChars(env, tc_jstrs[col_idx], NULL);
            col_pairs[i][j].from_column = fc_strs[col_idx];
            col_pairs[i][j].to_column = tc_strs[col_idx];
            col_idx++;
        }

        fks[i].from_table = ft_strs[i];
        fks[i].to_table = tt_strs[i];
        fks[i].columns = col_pairs[i];
        fks[i].column_count = nc;
    }

    char *err_msg = NULL;
    int err_line = 0, err_col = 0;
    char *result = sqldeep_transpile_fk(c_input, fks, fk_count,
                                         &err_msg, &err_line, &err_col);

    // Release all JNI strings.
    (*env)->ReleaseStringUTFChars(env, input, c_input);
    col_idx = 0;
    for (int i = 0; i < fk_count; i++) {
        (*env)->ReleaseStringUTFChars(env, ft_jstrs[i], ft_strs[i]);
        (*env)->ReleaseStringUTFChars(env, tt_jstrs[i], tt_strs[i]);
        int nc = fks[i].column_count;
        for (int j = 0; j < nc; j++) {
            (*env)->ReleaseStringUTFChars(env, fc_jstrs[col_idx], fc_strs[col_idx]);
            (*env)->ReleaseStringUTFChars(env, tc_jstrs[col_idx], tc_strs[col_idx]);
            col_idx++;
        }
        free(col_pairs[i]);
    }
    free(fks); free(col_pairs);
    free(ft_strs); free(tt_strs); free(ft_jstrs); free(tt_jstrs);
    free(fc_strs); free(tc_strs); free(fc_jstrs); free(tc_jstrs);

    if (!result) {
        jclass exc_cls = (*env)->FindClass(env,
            "com/marcelocantos/sqldeep/SQLDeep$TranspileException");
        if (exc_cls) {
            jmethodID ctor = (*env)->GetMethodID(env, exc_cls, "<init>",
                "(Ljava/lang/String;II)V");
            if (ctor) {
                jstring j_msg = (*env)->NewStringUTF(env,
                    err_msg ? err_msg : "unknown error");
                jobject exc = (*env)->NewObject(env, exc_cls, ctor,
                    j_msg, (jint)err_line, (jint)err_col);
                if (exc) (*env)->Throw(env, (jthrowable)exc);
            }
        }
        if (err_msg) sqldeep_free(err_msg);
        return NULL;
    }

    jstring j_result = (*env)->NewStringUTF(env, result);
    sqldeep_free(result);
    return j_result;
}

// ── SQLite direct access (for tests) ─────────────────────────────

JNIEXPORT jlong JNICALL
Java_com_marcelocantos_sqldeep_SQLDeepTestHelper_nativeOpenMemoryDB(
    JNIEnv *env, jclass cls) {
    (void)cls;
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        jclass exc = (*env)->FindClass(env, "java/lang/RuntimeException");
        (*env)->ThrowNew(env, exc, "sqlite3_open :memory: failed");
        return 0;
    }
    sqldeep_register_sqlite(db);
    return (jlong)(intptr_t)db;
}

JNIEXPORT void JNICALL
Java_com_marcelocantos_sqldeep_SQLDeepTestHelper_nativeCloseDB(
    JNIEnv *env, jclass cls, jlong dbHandle) {
    (void)env; (void)cls;
    sqlite3 *db = (sqlite3 *)(intptr_t)dbHandle;
    if (db) sqlite3_close(db);
}

JNIEXPORT void JNICALL
Java_com_marcelocantos_sqldeep_SQLDeepTestHelper_nativeExecSQL(
    JNIEnv *env, jclass cls, jlong dbHandle, jstring sql) {
    (void)cls;
    sqlite3 *db = (sqlite3 *)(intptr_t)dbHandle;
    const char *c_sql = (*env)->GetStringUTFChars(env, sql, NULL);
    if (!c_sql) return;

    char *err = NULL;
    int rc = sqlite3_exec(db, c_sql, NULL, NULL, &err);
    (*env)->ReleaseStringUTFChars(env, sql, c_sql);

    if (rc != SQLITE_OK) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "sqlite3_exec: %s", err ? err : "unknown");
        sqlite3_free(err);
        jclass exc = (*env)->FindClass(env, "java/lang/RuntimeException");
        (*env)->ThrowNew(env, exc, buf);
    }
}

JNIEXPORT jobjectArray JNICALL
Java_com_marcelocantos_sqldeep_SQLDeepTestHelper_nativeQueryRows(
    JNIEnv *env, jclass cls, jlong dbHandle, jstring sql) {
    (void)cls;
    sqlite3 *db = (sqlite3 *)(intptr_t)dbHandle;
    const char *c_sql = (*env)->GetStringUTFChars(env, sql, NULL);
    if (!c_sql) return NULL;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, c_sql, -1, &stmt, NULL);
    (*env)->ReleaseStringUTFChars(env, sql, c_sql);

    if (rc != SQLITE_OK) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "sqlite3_prepare_v2: %s", sqlite3_errmsg(db));
        jclass exc = (*env)->FindClass(env, "java/lang/RuntimeException");
        (*env)->ThrowNew(env, exc, buf);
        return NULL;
    }

    // Collect rows (first column only, matching C++/Go behavior).
    int capacity = 32;
    int count = 0;
    jstring *rows = calloc(capacity, sizeof(jstring));

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *text = (const char *)sqlite3_column_text(stmt, 0);
        if (count >= capacity) {
            capacity *= 2;
            rows = realloc(rows, capacity * sizeof(jstring));
        }
        rows[count++] = (*env)->NewStringUTF(env, text ? text : "NULL");
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        free(rows);
        char buf[1024];
        snprintf(buf, sizeof(buf), "sqlite3_step: %s", sqlite3_errmsg(db));
        jclass exc = (*env)->FindClass(env, "java/lang/RuntimeException");
        (*env)->ThrowNew(env, exc, buf);
        return NULL;
    }

    jclass str_cls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result = (*env)->NewObjectArray(env, count, str_cls, NULL);
    for (int i = 0; i < count; i++) {
        (*env)->SetObjectArrayElement(env, result, i, rows[i]);
    }
    free(rows);
    return result;
}

// ── Version ──────────────────────────────────────────────────────

JNIEXPORT jstring JNICALL
Java_com_marcelocantos_sqldeep_SQLDeep_nativeVersion(
    JNIEnv *env, jclass cls) {
    (void)cls;
    return (*env)->NewStringUTF(env, sqldeep_version());
}

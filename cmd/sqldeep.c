// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// sqldeep CLI: full SQLite shell with sqldeep transpilation and XML functions.
// Build by compiling this file (which #includes shell.c) and linking with
// sqldeep.o and sqlite3.o.

#include <string.h>
#include <sqlite3.h>
#include "sqldeep.h"

// ── XML runtime functions (sentinel-based) ─────────────────────────

static const char kXmlSentinel = '\x01';

static int is_xml_sentinel(const char *s) {
    return s && s[0] == kXmlSentinel;
}

static void xml_escape_text_to(const char *s, char *out, int *pos) {
    for (; *s; ++s) {
        switch (*s) {
        case '<': memcpy(out + *pos, "&lt;", 4); *pos += 4; break;
        case '>': memcpy(out + *pos, "&gt;", 4); *pos += 4; break;
        case '&': memcpy(out + *pos, "&amp;", 5); *pos += 5; break;
        default:  out[*pos] = *s; (*pos)++; break;
        }
    }
}

static int xml_escaped_len(const char *s) {
    int n = 0;
    for (; *s; ++s) {
        switch (*s) {
        case '<': n += 4; break;
        case '>': n += 4; break;
        case '&': n += 5; break;
        default:  n++; break;
        }
    }
    return n;
}

static void sd_xml_attrs(sqlite3_context *ctx, int argc,
                          sqlite3_value **argv) {
    int i, len = 1; /* sentinel */
    if (argc % 2 != 0) {
        sqlite3_result_error(ctx, "xml_attrs requires even number of args", -1);
        return;
    }
    /* Calculate length */
    for (i = 0; i < argc; i += 2) {
        const char *val;
        if (sqlite3_value_type(argv[i + 1]) == SQLITE_NULL) continue;
        len += 1; /* space */
        len += (int)strlen((const char *)sqlite3_value_text(argv[i]));
        val = (const char *)sqlite3_value_text(argv[i + 1]);
        len += 2 + xml_escaped_len(val); /* ="..." */
    }
    char *out = (char *)sqlite3_malloc(len + 1);
    if (!out) { sqlite3_result_error_nomem(ctx); return; }
    int pos = 0;
    out[pos++] = kXmlSentinel;
    for (i = 0; i < argc; i += 2) {
        const char *name, *val;
        if (sqlite3_value_type(argv[i + 1]) == SQLITE_NULL) continue;
        name = (const char *)sqlite3_value_text(argv[i]);
        val = (const char *)sqlite3_value_text(argv[i + 1]);
        out[pos++] = ' ';
        memcpy(out + pos, name, strlen(name));
        pos += (int)strlen(name);
        out[pos++] = '=';
        out[pos++] = '"';
        for (const char *p = val; *p; ++p) {
            switch (*p) {
            case '"': memcpy(out + pos, "&quot;", 6); pos += 6; break;
            case '<': memcpy(out + pos, "&lt;", 4); pos += 4; break;
            case '>': memcpy(out + pos, "&gt;", 4); pos += 4; break;
            case '&': memcpy(out + pos, "&amp;", 5); pos += 5; break;
            default:  out[pos++] = *p; break;
            }
        }
        out[pos++] = '"';
    }
    out[pos] = '\0';
    sqlite3_result_text(ctx, out, pos, sqlite3_free);
}

static void sd_xml_element(sqlite3_context *ctx, int argc,
                            sqlite3_value **argv) {
    if (argc < 1) {
        sqlite3_result_error(ctx, "xml_element requires at least 1 arg", -1);
        return;
    }
    const char *tag = (const char *)sqlite3_value_text(argv[0]);
    int taglen = (int)strlen(tag);
    const char *attrs = "";
    int attrslen = 0;
    int child_start = 1;

    if (argc > 1) {
        const char *a = (const char *)sqlite3_value_text(argv[1]);
        if (is_xml_sentinel(a) && a[1] == ' ') {
            attrs = a + 1;
            attrslen = (int)strlen(attrs);
            child_start = 2;
        }
    }

    /* Calculate output size */
    int has_children = 0;
    int children_len = 0;
    for (int i = child_start; i < argc; ++i) {
        const char *c;
        if (sqlite3_value_type(argv[i]) == SQLITE_NULL) continue;
        has_children = 1;
        c = (const char *)sqlite3_value_text(argv[i]);
        if (is_xml_sentinel(c)) {
            children_len += (int)strlen(c + 1);
        } else {
            children_len += xml_escaped_len(c);
        }
    }

    /* sentinel + <tag attrs> children </tag> + NUL */
    int outlen = 1 + 1 + taglen + attrslen +
                 (has_children ? 1 + children_len + 2 + taglen + 1 : 2) + 1;
    char *out = (char *)sqlite3_malloc(outlen);
    if (!out) { sqlite3_result_error_nomem(ctx); return; }
    int pos = 0;
    out[pos++] = kXmlSentinel;
    out[pos++] = '<';
    memcpy(out + pos, tag, taglen); pos += taglen;
    memcpy(out + pos, attrs, attrslen); pos += attrslen;

    if (has_children) {
        out[pos++] = '>';
        for (int i = child_start; i < argc; ++i) {
            const char *c;
            if (sqlite3_value_type(argv[i]) == SQLITE_NULL) continue;
            c = (const char *)sqlite3_value_text(argv[i]);
            if (is_xml_sentinel(c)) {
                int slen = (int)strlen(c + 1);
                memcpy(out + pos, c + 1, slen);
                pos += slen;
            } else {
                xml_escape_text_to(c, out, &pos);
            }
        }
        out[pos++] = '<';
        out[pos++] = '/';
        memcpy(out + pos, tag, taglen); pos += taglen;
        out[pos++] = '>';
    } else {
        out[pos++] = '/';
        out[pos++] = '>';
    }
    out[pos] = '\0';
    sqlite3_result_text(ctx, out, pos, sqlite3_free);
}

typedef struct XmlAggCtx {
    char *buf;
    int len;
    int cap;
} XmlAggCtx;

static void xml_agg_append(XmlAggCtx *p, const char *s, int n) {
    if (p->len + n >= p->cap) {
        int newcap = (p->cap + n) * 2 + 64;
        p->buf = (char *)sqlite3_realloc(p->buf, newcap);
        p->cap = newcap;
    }
    memcpy(p->buf + p->len, s, n);
    p->len += n;
}

static void sd_xml_agg_step(sqlite3_context *ctx, int argc,
                             sqlite3_value **argv) {
    (void)argc;
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) return;
    XmlAggCtx *p = (XmlAggCtx *)sqlite3_aggregate_context(ctx, sizeof(*p));
    if (!p) return;
    const char *v = (const char *)sqlite3_value_text(argv[0]);
    if (is_xml_sentinel(v)) {
        xml_agg_append(p, v + 1, (int)strlen(v + 1));
    } else {
        /* Escape plain text */
        for (const char *c = v; *c; ++c) {
            switch (*c) {
            case '<': xml_agg_append(p, "&lt;", 4); break;
            case '>': xml_agg_append(p, "&gt;", 4); break;
            case '&': xml_agg_append(p, "&amp;", 5); break;
            default: xml_agg_append(p, c, 1); break;
            }
        }
    }
}

static void sd_xml_agg_final(sqlite3_context *ctx) {
    XmlAggCtx *p = (XmlAggCtx *)sqlite3_aggregate_context(ctx, 0);
    if (!p || !p->buf || p->len == 0) {
        sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
        return;
    }
    /* Prepend sentinel */
    char *out = (char *)sqlite3_malloc(1 + p->len + 1);
    if (!out) { sqlite3_result_error_nomem(ctx); return; }
    out[0] = kXmlSentinel;
    memcpy(out + 1, p->buf, p->len);
    out[1 + p->len] = '\0';
    sqlite3_result_text(ctx, out, 1 + p->len, sqlite3_free);
    sqlite3_free(p->buf);
}

/* Auto-extension entry point: called on every new connection. */
static int sd_register_xml(sqlite3 *db, char **pzErrMsg,
                            const struct sqlite3_api_routines *pApi) {
    (void)pzErrMsg; (void)pApi;
    sqlite3_create_function(db, "xml_element", -1, SQLITE_UTF8,
                            0, sd_xml_element, 0, 0);
    sqlite3_create_function(db, "xml_attrs", -1, SQLITE_UTF8,
                            0, sd_xml_attrs, 0, 0);
    sqlite3_create_function(db, "xml_agg", 1, SQLITE_UTF8,
                            0, 0, sd_xml_agg_step, sd_xml_agg_final);
    return SQLITE_OK;
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

// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Interactive SQLite shell with sqldeep transpilation and XML functions.
// Usage: sqldeep-shell [database.db]

#include <sqlite3.h>
#include "sqldeep.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// ── XML runtime functions ──────────────────────────────────────────

static const char kXmlSentinel = '\x01';

static bool is_xml_sentinel(const char* s) {
    return s && s[0] == kXmlSentinel;
}

static std::string xml_escape_text(const char* s) {
    std::string out;
    for (; *s; ++s) {
        switch (*s) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default:  out += *s; break;
        }
    }
    return out;
}

static std::string xml_escape_attr(const char* s) {
    std::string out;
    for (; *s; ++s) {
        switch (*s) {
        case '"': out += "&quot;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default:  out += *s; break;
        }
    }
    return out;
}

static void sqlite_xml_attrs(sqlite3_context* ctx, int argc,
                              sqlite3_value** argv) {
    if (argc % 2 != 0) {
        sqlite3_result_error(ctx, "xml_attrs requires even number of args", -1);
        return;
    }
    std::string out;
    for (int i = 0; i < argc; i += 2) {
        if (sqlite3_value_type(argv[i + 1]) == SQLITE_NULL) continue;
        const char* name = reinterpret_cast<const char*>(
            sqlite3_value_text(argv[i]));
        int vtype = sqlite3_value_type(argv[i + 1]);
        if (vtype == SQLITE_INTEGER) {
            int v = sqlite3_value_int(argv[i + 1]);
            if (v == 1) {
                out += " ";
                out += name;
            }
        } else {
            const char* val = reinterpret_cast<const char*>(
                sqlite3_value_text(argv[i + 1]));
            out += " ";
            out += name;
            out += "=\"";
            out += xml_escape_attr(val);
            out += "\"";
        }
    }
    std::string result = std::string(1, kXmlSentinel) + out;
    sqlite3_result_text(ctx, result.c_str(), -1, SQLITE_TRANSIENT);
}

static void sqlite_xml_element(sqlite3_context* ctx, int argc,
                                sqlite3_value** argv) {
    if (argc < 1) {
        sqlite3_result_error(ctx, "xml_element requires at least 1 arg", -1);
        return;
    }
    const char* tag = reinterpret_cast<const char*>(
        sqlite3_value_text(argv[0]));
    std::string attrs_str;
    int child_start = 1;

    if (argc > 1) {
        const char* a = reinterpret_cast<const char*>(
            sqlite3_value_text(argv[1]));
        if (is_xml_sentinel(a) && a[1] == ' ') {
            attrs_str = a + 1;
            child_start = 2;
        }
    }

    std::string children;
    bool has_children = false;
    for (int i = child_start; i < argc; ++i) {
        if (sqlite3_value_type(argv[i]) == SQLITE_NULL) continue;
        has_children = true;
        const char* c = reinterpret_cast<const char*>(
            sqlite3_value_text(argv[i]));
        if (is_xml_sentinel(c)) {
            children += c + 1;
        } else {
            children += xml_escape_text(c);
        }
    }

    std::string out;
    out += kXmlSentinel;
    out += "<";
    out += tag;
    out += attrs_str;
    if (has_children) {
        out += ">";
        out += children;
        out += "</";
        out += tag;
        out += ">";
    } else {
        out += "/>";
    }

    sqlite3_result_text(ctx, out.c_str(), -1, SQLITE_TRANSIENT);
}

struct XmlAggCtx {
    std::string accum;
};

static void sqlite_xml_agg_step(sqlite3_context* ctx, int /*argc*/,
                                 sqlite3_value** argv) {
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) return;
    auto** pp = reinterpret_cast<XmlAggCtx**>(
        sqlite3_aggregate_context(ctx, sizeof(XmlAggCtx*)));
    if (!*pp) *pp = new XmlAggCtx();
    const char* v = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    if (is_xml_sentinel(v)) {
        (*pp)->accum += v + 1;
    } else {
        (*pp)->accum += xml_escape_text(v);
    }
}

static void sqlite_xml_agg_final(sqlite3_context* ctx) {
    auto** pp = reinterpret_cast<XmlAggCtx**>(
        sqlite3_aggregate_context(ctx, 0));
    if (!pp || !*pp) {
        sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
        return;
    }
    std::string result = std::string(1, kXmlSentinel) + (*pp)->accum;
    sqlite3_result_text(ctx, result.c_str(), -1, SQLITE_TRANSIENT);
    delete *pp;
}

static void register_xml_functions(sqlite3* db) {
    sqlite3_create_function(db, "xml_element", -1, SQLITE_UTF8,
                            nullptr, sqlite_xml_element, nullptr, nullptr);
    sqlite3_create_function(db, "xml_attrs", -1, SQLITE_UTF8,
                            nullptr, sqlite_xml_attrs, nullptr, nullptr);
    sqlite3_create_function(db, "xml_agg", 1, SQLITE_UTF8,
                            nullptr, nullptr,
                            sqlite_xml_agg_step, sqlite_xml_agg_final);
}

// ── REPL ───────────────────────────────────────────────────────────

static int print_row(void* /*data*/, int ncols, char** vals, char** /*cols*/) {
    for (int i = 0; i < ncols; ++i) {
        if (i > 0) std::cout << "|";
        const char* v = vals[i];
        if (!v) {
            std::cout << "NULL";
        } else if (is_xml_sentinel(v)) {
            std::cout << (v + 1); // strip sentinel for display
        } else {
            std::cout << v;
        }
    }
    std::cout << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    const char* db_path = argc > 1 ? argv[1] : ":memory:";

    sqlite3* db = nullptr;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        std::cerr << "error: cannot open database: "
                  << sqlite3_errmsg(db) << "\n";
        return 1;
    }
    register_xml_functions(db);

    std::cerr << "sqldeep shell (SQLite " << sqlite3_libversion()
              << " + sqldeep " << sqldeep_version() << ")\n";
    std::cerr << "Enter sqldeep or plain SQL. Ctrl-D to exit.\n";

    std::string line;
    std::string buf;
    while (true) {
        std::cerr << (buf.empty() ? "sqldeep> " : "    ...> ");
        if (!std::getline(std::cin, line)) break;

        // Dot-commands pass through to special handling
        if (buf.empty() && !line.empty() && line[0] == '.') {
            if (line == ".quit" || line == ".exit") break;
            if (line == ".help") {
                std::cerr << ".quit     Exit\n"
                          << ".sql      Show transpiled SQL for next query\n"
                          << ".tables   List tables\n";
                continue;
            }
            if (line == ".tables") {
                char* err = nullptr;
                sqlite3_exec(db,
                    "SELECT name FROM sqlite_master WHERE type='table' "
                    "ORDER BY name;",
                    print_row, nullptr, &err);
                if (err) { std::cerr << "error: " << err << "\n"; sqlite3_free(err); }
                continue;
            }
            if (line == ".sql") {
                // Read next query and show transpiled SQL
                std::cerr << "   sql > ";
                std::string q;
                if (!std::getline(std::cin, q)) break;
                char* err_msg = nullptr;
                int err_line = 0, err_col = 0;
                char* result = sqldeep_transpile(q.c_str(),
                    &err_msg, &err_line, &err_col);
                if (result) {
                    std::cout << result << "\n";
                    sqldeep_free(result);
                } else {
                    std::cerr << "transpile error";
                    if (err_msg) {
                        std::cerr << ": " << err_msg;
                        sqldeep_free(err_msg);
                    }
                    std::cerr << " (line " << err_line
                              << ", col " << err_col << ")\n";
                }
                continue;
            }
            std::cerr << "unknown command: " << line << "\n";
            continue;
        }

        buf += line;

        // Accumulate until we see a semicolon at the end
        // (simple heuristic — doesn't handle semicolons inside strings)
        {
            auto end = buf.find_last_not_of(" \t\r\n");
            if (end == std::string::npos || buf[end] != ';') {
                buf += "\n";
                continue;
            }
        }

        // Transpile
        char* err_msg = nullptr;
        int err_line = 0, err_col = 0;
        char* transpiled = sqldeep_transpile(buf.c_str(),
            &err_msg, &err_line, &err_col);
        if (!transpiled) {
            std::cerr << "transpile error";
            if (err_msg) {
                std::cerr << ": " << err_msg;
                sqldeep_free(err_msg);
            }
            std::cerr << " (line " << err_line
                      << ", col " << err_col << ")\n";
            buf.clear();
            continue;
        }

        // Execute
        char* sql_err = nullptr;
        sqlite3_exec(db, transpiled, print_row, nullptr, &sql_err);
        if (sql_err) {
            std::cerr << "SQL error: " << sql_err << "\n";
            sqlite3_free(sql_err);
        }

        sqldeep_free(transpiled);
        buf.clear();
    }

    sqlite3_close(db);
    return 0;
}

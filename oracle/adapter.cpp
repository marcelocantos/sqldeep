// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Differential-harness adapter for sqldeep transpiler parity. One source,
// compiled twice: against oracle/ref/sqldeep.cpp (the frozen v0.21.0
// hand-written transpiler — the REFERENCE) and against dist/sqldeep.cpp (the
// deepparser AST pipeline — the PORT). Both expose the same C API.
//
// Wire: TSV (see ~/think/differential-harness/SPEC.md §2). The tape is a
// script — one statement per tick — run against a private in-memory SQLite:
//   kind=exec    raw SQL executed as-is (schema + data; shared state)
//   kind=xexec   sqldeep input, transpiled then executed (views, snapshots)
//   kind=query   sqldeep input, transpiled then queried; rows are the state
// Divergence compounds across ticks through the database: a wrong view at
// tick k shows up in every later query that reads it, which is what gives a
// transpiler a horizon at all.
//
// Trace per tick: status (ok | transpile_error | exec_error), nrows, rows
// (result rows, columns joined by '|', rows by newline), sql (transpiled
// output, whitespace-collapsed — informational: the two unparsers differ by
// design, execution results must not).
//
// DH_PLANT (selftest, oracle-first rule 12):
//   neg-order   every ORDER BY key negated (sort direction structurally wrong)
//   stale-view  xexec statements silently skipped (state never materialises)

#include "sqldeep.h"

#include <sqlite3.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

const char* const kFieldsTag = "@fields";

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> cells;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { cells.push_back(cur); cur.clear(); }
        else cur += c;
    }
    cells.push_back(cur);
    return cells;
}

std::string tsvUnescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) { out += s[i]; continue; }
        switch (s[++i]) {
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            default:  out += s[i]; break;
        }
    }
    return out;
}

std::string tsvEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string collapseWhitespace(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
        s.replace(pos, from.size(), to);
}

struct Tick { std::string status, rows, sql; int nrows = 0; };

struct Session {
    sqlite3* db = nullptr;
    std::string plant;

    Session() {
        sqlite3_open(":memory:", &db);
        if (const char* p = std::getenv("DH_PLANT")) plant = p;
    }
    ~Session() { sqlite3_close(db); }

    Tick exec(const std::string& sql) {
        Tick t;
        char* err = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) == SQLITE_OK) {
            t.status = "ok";
        } else {
            t.status = "exec_error";
            t.rows = err ? err : "";
        }
        sqlite3_free(err);
        return t;
    }

    Tick query(const std::string& sql) {
        Tick t;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            t.status = "exec_error";
            t.rows = sqlite3_errmsg(db);
            return t;
        }
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (t.nrows++) t.rows += '\n';
            const int n = sqlite3_column_count(stmt);
            for (int i = 0; i < n; ++i) {
                if (i) t.rows += '|';
                const unsigned char* txt = sqlite3_column_text(stmt, i);
                t.rows += txt ? reinterpret_cast<const char*>(txt) : "NULL";
            }
        }
        if (rc == SQLITE_DONE) {
            t.status = "ok";
        } else {
            t.status = "exec_error";
            t.rows = sqlite3_errmsg(db);
        }
        sqlite3_finalize(stmt);
        return t;
    }

    Tick transpiled(const std::string& kind, const std::string& input) {
        char* err = nullptr;
        int line = 0, col = 0;
        char* out = sqldeep_transpile_backend(input.c_str(), SQLDEEP_SQLITE_VANILLA,
                                              &err, &line, &col);
        if (!out) {
            Tick t;
            t.status = "transpile_error";
            t.rows = err ? err : "";
            sqldeep_free(err);
            return t;
        }
        std::string sql = out;
        sqldeep_free(out);
        if (plant == "neg-order") replaceAll(sql, "ORDER BY ", "ORDER BY -");
        Tick t;
        if (kind == "xexec") {
            t = (plant == "stale-view") ? Tick{"ok", "", "", 0} : exec(sql);
        } else {
            t = query(sql);
        }
        t.sql = collapseWhitespace(sql);
        return t;
    }
};

} // namespace

int main() {
    std::string line;
    if (!std::getline(std::cin, line)) return 0;
    const auto header = splitTabs(line);
    int kindCol = -1, sqlCol = -1;
    if (header.empty() || header[0] != kFieldsTag) {
        std::cerr << "adapter: tape must start with " << kFieldsTag << "\n";
        return 1;
    }
    for (size_t i = 1; i < header.size(); ++i) {
        if (header[i] == "kind") kindCol = int(i) - 1;
        if (header[i] == "sql")  sqlCol  = int(i) - 1;
    }
    if (kindCol < 0 || sqlCol < 0) {
        std::cerr << "adapter: tape needs kind and sql fields\n";
        return 1;
    }
    std::cout << kFieldsTag << "\tstatus\tnrows\trows\tsql\n";
    Session session;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const auto cells = splitTabs(line);
        if (int(cells.size()) <= std::max(kindCol, sqlCol)) {
            std::cerr << "adapter: short tape row\n";
            return 1;
        }
        const std::string kind = cells[kindCol];
        const std::string sql = tsvUnescape(cells[sqlCol]);
        const Tick t = (kind == "exec") ? session.exec(sql) : session.transpiled(kind, sql);
        std::cout << t.status << '\t' << t.nrows << '\t' << tsvEscape(t.rows) << '\t'
                  << tsvEscape(t.sql) << '\n';
    }
    return 0;
}

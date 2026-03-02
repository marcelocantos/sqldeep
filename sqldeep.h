// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#pragma once

#define SQLDEEP_VERSION       "0.4.0"
#define SQLDEEP_VERSION_MAJOR 0
#define SQLDEEP_VERSION_MINOR 4
#define SQLDEEP_VERSION_PATCH 0

#include <stdexcept>
#include <string>
#include <vector>

namespace sqldeep {

/// Describes a foreign key relationship from a child table to a parent table.
struct ForeignKey {
    std::string from_table;   // child table (has the FK column(s))
    std::string to_table;     // parent/referenced table

    struct ColumnPair {
        std::string from_column;  // FK column in child
        std::string to_column;    // referenced column in parent
    };
    std::vector<ColumnPair> columns;  // supports multi-column FKs
};

class Error : public std::runtime_error {
public:
    Error(const std::string& msg, int line, int col)
        : std::runtime_error(msg), line_(line), col_(col) {}
    int line() const { return line_; }
    int col() const { return col_; }
private:
    int line_;
    int col_;
};

/// Transpile sqldeep syntax to standard SQL with SQLite JSON functions.
/// Join paths (-> / <-) use the <table>_id naming convention.
/// Throws sqldeep::Error on parse errors.
std::string transpile(const std::string& input);

/// Transpile with explicit foreign key metadata.
/// Join paths are resolved from the provided FKs — no convention fallback.
/// Throws sqldeep::Error if a join cannot be resolved or is ambiguous.
std::string transpile(const std::string& input,
                      const std::vector<ForeignKey>& foreign_keys);

} // namespace sqldeep

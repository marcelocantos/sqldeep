// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#pragma once

#define SQLDEEP_VERSION       "0.3.0"
#define SQLDEEP_VERSION_MAJOR 0
#define SQLDEEP_VERSION_MINOR 3
#define SQLDEEP_VERSION_PATCH 0

#include <stdexcept>
#include <string>

namespace sqldeep {

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
/// Throws sqldeep::Error on parse errors.
std::string transpile(const std::string& input);

} // namespace sqldeep

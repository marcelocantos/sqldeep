// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#include "sqldeep.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sqldeep {
namespace {

static constexpr int kMaxNestingDepth = 200;

// ── Lexer ───────────────────────────────────────────────────────────

enum class TokenType {
    Ident,       // unquoted identifier or keyword
    DqString,    // double-quoted string "..."
    SqString,    // single-quoted string '...'
    Number,      // numeric literal
    LBrace,      // {
    RBrace,      // }
    LBracket,    // [
    RBracket,    // ]
    LParen,      // (
    RParen,      // )
    Comma,       // ,
    Colon,       // :
    Semicolon,   // ;
    Other,       // any other character or operator
    Eof,
};

struct Token {
    TokenType type;
    std::string text;
    int line;
    int col;
    size_t src_begin; // offset in source where token text starts
    size_t src_end;   // offset right after token text ends
};

struct LexerState {
    size_t pos;
    int line;
    int col;
};

class Lexer {
public:
    explicit Lexer(const std::string& input)
        : src_(input), pos_(0), line_(1), col_(1) {}

    Token next() {
        skip_whitespace_and_comments();
        if (pos_ >= src_.size())
            return {TokenType::Eof, "", line_, col_, pos_, pos_};

        int tline = line_, tcol = col_;
        size_t begin = pos_;
        char c = src_[pos_];

        switch (c) {
        case '{': advance(); return {TokenType::LBrace,   "{", tline, tcol, begin, pos_};
        case '}': advance(); return {TokenType::RBrace,   "}", tline, tcol, begin, pos_};
        case '[': advance(); return {TokenType::LBracket, "[", tline, tcol, begin, pos_};
        case ']': advance(); return {TokenType::RBracket, "]", tline, tcol, begin, pos_};
        case '(': advance(); return {TokenType::LParen,   "(", tline, tcol, begin, pos_};
        case ')': advance(); return {TokenType::RParen,   ")", tline, tcol, begin, pos_};
        case ',': advance(); return {TokenType::Comma,    ",", tline, tcol, begin, pos_};
        case ':': advance(); return {TokenType::Colon,    ":", tline, tcol, begin, pos_};
        case ';': advance(); return {TokenType::Semicolon,";", tline, tcol, begin, pos_};
        case '\'': return lex_string('\'', TokenType::SqString, tline, tcol, begin);
        case '"':  return lex_string('"',  TokenType::DqString,  tline, tcol, begin);
        default: break;
        }

        if (is_ident_start(c)) return lex_ident(tline, tcol, begin);
        if (is_digit(c) || (c == '.' && pos_ + 1 < src_.size() && is_digit(src_[pos_ + 1])))
            return lex_number(tline, tcol, begin);

        // Operator or other character
        std::string s(1, c);
        advance();
        if (pos_ < src_.size()) {
            char n = src_[pos_];
            if ((c == '<' && (n == '=' || n == '>')) ||
                (c == '>' && n == '=') ||
                (c == '!' && n == '=') ||
                (c == '|' && n == '|') ||
                (c == '<' && n == '<') ||
                (c == '>' && n == '>') ||
                (c == '-' && n == '>')) {
                s += n;
                advance();
            }
        }
        return {TokenType::Other, s, tline, tcol, begin, pos_};
    }

    Token peek() {
        auto st = save();
        Token t = next();
        restore(st);
        return t;
    }

    LexerState save() const { return {pos_, line_, col_}; }
    void restore(const LexerState& st) { pos_ = st.pos; line_ = st.line; col_ = st.col; }

    // Current position in source (right after last consumed token).
    size_t offset() const { return pos_; }

    const std::string& source() const { return src_; }

    [[noreturn]] void error(const std::string& msg) {
        throw Error(msg, line_, col_);
    }

    [[noreturn]] void error(const std::string& msg, int line, int col) {
        throw Error(msg, line, col);
    }

private:
    void advance() {
        if (pos_ < src_.size()) {
            if (src_[pos_] == '\n') { ++line_; col_ = 1; }
            else { ++col_; }
            ++pos_;
        }
    }

    void skip_whitespace_and_comments() {
        while (pos_ < src_.size()) {
            if (std::isspace(static_cast<unsigned char>(src_[pos_]))) {
                advance();
            } else if (pos_ + 1 < src_.size() && src_[pos_] == '/' && src_[pos_ + 1] == '/') {
                advance(); advance();
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
            } else {
                break;
            }
        }
    }

    Token lex_string(char quote, TokenType type, int tline, int tcol, size_t begin) {
        std::string s(1, quote);
        advance(); // skip opening quote
        while (pos_ < src_.size()) {
            if (src_[pos_] == quote) {
                // SQL doubled-quote escape: '' inside '...' or "" inside "..."
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == quote) {
                    s += quote; advance();
                    s += quote; advance();
                    continue;
                }
                break; // end of string
            }
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                s += src_[pos_]; advance();
                s += src_[pos_]; advance();
            } else {
                s += src_[pos_]; advance();
            }
        }
        if (pos_ >= src_.size()) error("unterminated string literal", tline, tcol);
        s += quote;
        advance(); // skip closing quote
        return {type, s, tline, tcol, begin, pos_};
    }

    Token lex_ident(int tline, int tcol, size_t begin) {
        std::string s;
        while (pos_ < src_.size() && is_ident_cont(src_[pos_])) {
            s += src_[pos_]; advance();
        }
        return {TokenType::Ident, s, tline, tcol, begin, pos_};
    }

    Token lex_number(int tline, int tcol, size_t begin) {
        std::string s;
        while (pos_ < src_.size() && (is_digit(src_[pos_]) || src_[pos_] == '.')) {
            s += src_[pos_]; advance();
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            s += src_[pos_]; advance();
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) {
                s += src_[pos_]; advance();
            }
            while (pos_ < src_.size() && is_digit(src_[pos_])) {
                s += src_[pos_]; advance();
            }
        }
        return {TokenType::Number, s, tline, tcol, begin, pos_};
    }

    static bool is_ident_start(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }
    static bool is_ident_cont(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }
    static bool is_digit(char c) {
        return std::isdigit(static_cast<unsigned char>(c));
    }

    const std::string& src_;
    size_t pos_;
    int line_;
    int col_;
};

// ── AST ─────────────────────────────────────────────────────────────

struct DeepSelect;
struct ObjectLiteral;
struct ArrayLiteral;
struct AutoJoin;

using SqlPart = std::variant<
    std::string,
    std::unique_ptr<DeepSelect>,
    std::unique_ptr<ObjectLiteral>,
    std::unique_ptr<ArrayLiteral>,
    std::unique_ptr<AutoJoin>
>;
using SqlParts = std::vector<SqlPart>;

struct ObjectLiteral {
    struct Field {
        std::string key;
        SqlParts value; // empty = bare field
    };
    std::vector<Field> fields;
};

struct ArrayLiteral {
    std::vector<SqlParts> elements;
};

struct AutoJoin {
    std::string parent_alias;  // e.g. "c"
    std::string parent_table;  // e.g. "customers"
    std::string child_table;   // e.g. "orders"
    std::string child_alias;   // e.g. "o" (empty if none)
};

struct DeepSelect {
    std::variant<ObjectLiteral, ArrayLiteral> projection;
    SqlParts tail;
};

// ── Parser ──────────────────────────────────────────────────────────

static bool is_keyword(const Token& t, const char* kw) {
    if (t.type != TokenType::Ident) return false;
    const auto& s = t.text;
    size_t len = std::strlen(kw);
    if (s.size() != len) return false;
    for (size_t i = 0; i < len; ++i) {
        if (std::toupper(static_cast<unsigned char>(s[i])) !=
            std::toupper(static_cast<unsigned char>(kw[i])))
            return false;
    }
    return true;
}

static bool is_sql_keyword(const std::string& s) {
    static const char* keywords[] = {
        "SELECT", "FROM", "WHERE", "JOIN", "INNER", "LEFT", "RIGHT",
        "OUTER", "CROSS", "NATURAL", "ON", "ORDER", "GROUP", "HAVING",
        "LIMIT", "UNION", "INTERSECT", "EXCEPT", "AS", "AND", "OR",
        "NOT", "IN", "IS", "NULL", "LIKE", "BETWEEN", "EXISTS",
        "CASE", "WHEN", "THEN", "ELSE", "END", "SET", "INTO",
        "VALUES", "INSERT", "UPDATE", "DELETE", "DISTINCT", "ALL",
        "ASC", "DESC", "BY", "OFFSET", "FETCH", "FOR", "WITH",
    };
    for (const char* kw : keywords) {
        if (is_keyword({TokenType::Ident, s, 0, 0, 0, 0}, kw))
            return true;
    }
    return false;
}

static bool is_from_or_join(const Token& t) {
    return is_keyword(t, "FROM") || is_keyword(t, "JOIN");
}

// Pre-scan input to build alias → table name map.
static std::unordered_map<std::string, std::string>
build_alias_map(const std::string& input) {
    std::unordered_map<std::string, std::string> map;
    Lexer lex(input);
    int paren_depth = 0;

    while (true) {
        Token t = lex.next();
        if (t.type == TokenType::Eof) break;

        if (t.type == TokenType::LParen) { ++paren_depth; continue; }
        if (t.type == TokenType::RParen) {
            if (paren_depth > 0) --paren_depth;
            continue;
        }

        // Only look for aliases at paren depth 0.
        if (paren_depth > 0) continue;

        if (!is_from_or_join(t)) continue;

        // After FROM/JOIN, expect table name or alias->child pattern.
        Token first = lex.peek();
        if (first.type != TokenType::Ident) continue;
        lex.next(); // consume first ident

        Token second = lex.peek();

        // Pattern: ident -> ident [alias]
        if (second.type == TokenType::Other && second.text == "->") {
            lex.next(); // consume ->
            Token child = lex.peek();
            if (child.type != TokenType::Ident) continue;
            lex.next(); // consume child table
            Token alias = lex.peek();
            if (alias.type == TokenType::Ident && !is_sql_keyword(alias.text)) {
                lex.next();
                map[alias.text] = child.text;
            }
            continue;
        }

        // Pattern: ident AS ident
        if (is_keyword(second, "AS")) {
            lex.next(); // consume AS
            Token alias = lex.peek();
            if (alias.type == TokenType::Ident) {
                lex.next();
                map[alias.text] = first.text;
            }
            continue;
        }

        // Pattern: ident ident (table alias)
        if (second.type == TokenType::Ident && !is_sql_keyword(second.text)) {
            lex.next();
            map[second.text] = first.text;
            continue;
        }
    }

    return map;
}

class Parser {
public:
    Parser(Lexer& lex, std::unordered_map<std::string, std::string> alias_map)
        : lex_(lex), alias_map_(std::move(alias_map)) {}

    SqlParts parse_document() {
        return parse_sql_parts(/*stop_comma=*/false,
                               /*stop_rbrace=*/false,
                               /*stop_rbracket=*/false,
                               /*stop_rparen=*/false,
                               /*depth=*/0);
    }

private:
    void check_depth(int depth, int line, int col) {
        if (depth > kMaxNestingDepth)
            lex_.error("maximum nesting depth exceeded", line, col);
    }

    // Parse a sequence of SQL fragments interleaved with deep constructs.
    SqlParts parse_sql_parts(bool stop_comma,
                             bool stop_rbrace,
                             bool stop_rbracket,
                             bool stop_rparen,
                             int depth) {
        SqlParts parts;
        std::string accum;
        size_t last_end = 0; // src position after last consumed raw token
        bool has_raw = false;

        auto flush = [&]() {
            if (!accum.empty()) {
                parts.push_back(std::move(accum));
                accum.clear();
            }
            has_raw = false;
        };

        // Flush accumulated raw SQL, preserving spacing before the
        // deep construct whose first source token is next_tok.
        auto flush_before = [&](const Token& next_tok) {
            if (has_raw && last_end < next_tok.src_begin)
                accum += " ";
            flush();
        };

        bool need_space = false; // space needed after a non-string AST part

        auto accum_token = [&](const Token& tok) {
            if (has_raw) {
                // Add space only if there was whitespace/comments in source
                if (last_end < tok.src_begin) accum += " ";
            } else if (need_space && last_end < tok.src_begin) {
                accum += " ";
            }
            accum += tok.text;
            last_end = tok.src_end;
            has_raw = true;
            need_space = false;
        };

        int paren_depth = 0;

        while (true) {
            Token t = lex_.peek();

            if (t.type == TokenType::Eof) break;

            // Stop conditions at paren depth 0
            if (paren_depth == 0) {
                if (stop_comma && t.type == TokenType::Comma) break;
                if (stop_rbrace && t.type == TokenType::RBrace) break;
                if (stop_rbracket && t.type == TokenType::RBracket) break;
                if (stop_rparen && t.type == TokenType::RParen) break;
            }

            // Semicolons at depth 0 pass through at top level, stop otherwise
            if (t.type == TokenType::Semicolon && paren_depth == 0) {
                if (!stop_comma && !stop_rbrace && !stop_rbracket && !stop_rparen) {
                    Token tok = lex_.next();
                    accum_token(tok);
                    continue;
                }
                break;
            }

            // Check for (SELECT {/[) pattern — subquery with deep construct
            if (t.type == TokenType::LParen && paren_depth == 0) {
                auto st = lex_.save();
                lex_.next(); // consume (
                Token t2 = lex_.peek();
                if (is_keyword(t2, "SELECT")) {
                    lex_.next(); // consume SELECT
                    Token t3 = lex_.peek();
                    if (t3.type == TokenType::LBrace || t3.type == TokenType::LBracket) {
                        // Found (SELECT {/[)
                        flush_before(t);
                        auto ds = parse_deep_select(t2,
                            /*stop_comma=*/false, /*stop_rbrace=*/false,
                            /*stop_rbracket=*/false, /*stop_rparen=*/true,
                            depth + 1);
                        Token rp = lex_.next(); // consume )
                        if (rp.type != TokenType::RParen)
                            lex_.error("expected ')' after subquery", rp.line, rp.col);
                        parts.push_back(std::move(ds));
                        last_end = rp.src_end;
                        need_space = true;
                        continue;
                    }
                }
                // Not (SELECT {/[, restore and handle normally
                lex_.restore(st);
            }

            // Check for SELECT {/[ at depth 0 (top-level deep select)
            if (is_keyword(t, "SELECT") && paren_depth == 0) {
                auto st = lex_.save();
                lex_.next(); // consume SELECT
                Token t2 = lex_.peek();
                if (t2.type == TokenType::LBrace || t2.type == TokenType::LBracket) {
                    flush_before(t);
                    Token sel = {TokenType::Ident, "SELECT", t.line, t.col,
                                 t.src_begin, t.src_end};
                    auto ds = parse_deep_select(sel, stop_comma, stop_rbrace,
                                                stop_rbracket, stop_rparen,
                                                depth + 1);
                    parts.push_back(std::move(ds));
                    last_end = lex_.offset();
                    need_space = true;
                    continue;
                }
                // Not deep — restore and accumulate SELECT as raw SQL
                lex_.restore(st);
            }

            // Check for inline { or [ at depth 0
            if (paren_depth == 0 && t.type == TokenType::LBrace) {
                flush_before(t);
                auto obj = parse_object_literal(depth + 1);
                parts.push_back(std::move(obj));
                last_end = lex_.offset();
                need_space = true;
                continue;
            }

            if (paren_depth == 0 && t.type == TokenType::LBracket) {
                flush_before(t);
                auto arr = parse_array_literal(depth + 1);
                parts.push_back(std::move(arr));
                last_end = lex_.offset();
                need_space = true;
                continue;
            }

            // Track paren depth
            if (t.type == TokenType::LParen) ++paren_depth;
            if (t.type == TokenType::RParen) {
                if (paren_depth == 0)
                    lex_.error("unmatched ')'", t.line, t.col);
                --paren_depth;
            }

            // Check for ident -> ident (auto-join)
            if (t.type == TokenType::Ident && paren_depth == 0) {
                auto st = lex_.save();
                Token alias_tok = lex_.next(); // consume ident
                Token arrow = lex_.peek();
                if (arrow.type == TokenType::Other && arrow.text == "->") {
                    lex_.next(); // consume ->
                    Token child = lex_.peek();
                    if (child.type == TokenType::Ident) {
                        lex_.next(); // consume child table
                        auto it = alias_map_.find(alias_tok.text);
                        if (it == alias_map_.end())
                            lex_.error("unknown table alias '" +
                                       alias_tok.text + "'",
                                       alias_tok.line, alias_tok.col);
                        auto aj = std::make_unique<AutoJoin>();
                        aj->parent_alias = alias_tok.text;
                        aj->parent_table = it->second;
                        aj->child_table = child.text;
                        // Optional child alias
                        Token next = lex_.peek();
                        if (next.type == TokenType::Ident &&
                            !is_sql_keyword(next.text)) {
                            lex_.next();
                            aj->child_alias = next.text;
                        }
                        flush_before(alias_tok);
                        parts.push_back(std::move(aj));
                        last_end = lex_.offset();
                        need_space = true;
                        continue;
                    }
                }
                lex_.restore(st);
            }

            // Accumulate raw SQL token
            Token tok = lex_.next();
            accum_token(tok);
        }

        flush();
        return parts;
    }

    // Parse deep select — SELECT keyword has already been consumed.
    std::unique_ptr<DeepSelect> parse_deep_select(
            const Token& select_tok,
            bool stop_comma, bool stop_rbrace,
            bool stop_rbracket, bool stop_rparen,
            int depth) {
        check_depth(depth, select_tok.line, select_tok.col);
        auto ds = std::make_unique<DeepSelect>();

        Token t = lex_.peek();
        if (t.type == TokenType::LBrace) {
            ds->projection = std::move(*parse_object_literal(depth));
        } else if (t.type == TokenType::LBracket) {
            ds->projection = std::move(*parse_array_literal(depth));
        } else {
            lex_.error("expected '{' or '[' after SELECT",
                       select_tok.line, select_tok.col);
        }

        ds->tail = parse_sql_parts(stop_comma, stop_rbrace,
                                   stop_rbracket, stop_rparen, depth);
        return ds;
    }

    std::unique_ptr<ObjectLiteral> parse_object_literal(int depth) {
        Token lbrace = lex_.next();
        assert(lbrace.type == TokenType::LBrace);
        check_depth(depth, lbrace.line, lbrace.col);

        auto obj = std::make_unique<ObjectLiteral>();

        while (true) {
            Token t = lex_.peek();
            if (t.type == TokenType::RBrace) { lex_.next(); break; }
            if (t.type == TokenType::Eof)
                lex_.error("unterminated '{'", lbrace.line, lbrace.col);

            obj->fields.push_back(parse_field(depth));

            t = lex_.peek();
            if (t.type == TokenType::Comma) {
                lex_.next();
            } else if (t.type != TokenType::RBrace) {
                lex_.error("expected ',' or '}' in object literal");
            }
        }

        return obj;
    }

    ObjectLiteral::Field parse_field(int depth) {
        ObjectLiteral::Field field;

        Token key = lex_.next();
        if (key.type == TokenType::Ident) {
            field.key = key.text;
        } else if (key.type == TokenType::DqString) {
            // Strip outer quotes and unescape \" → " and \\ → \.
            auto raw = key.text.substr(1, key.text.size() - 2);
            field.key.reserve(raw.size());
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size() &&
                    (raw[i + 1] == '"' || raw[i + 1] == '\\')) {
                    field.key += raw[++i];
                } else if (raw[i] == '"' && i + 1 < raw.size() && raw[i + 1] == '"') {
                    field.key += '"';
                    ++i; // skip doubled quote
                } else {
                    field.key += raw[i];
                }
            }
        } else {
            lex_.error("expected field name (identifier or double-quoted string)",
                       key.line, key.col);
        }

        Token t = lex_.peek();
        if (t.type == TokenType::Colon) {
            lex_.next();
            field.value = parse_sql_parts(/*stop_comma=*/true,
                                          /*stop_rbrace=*/true,
                                          /*stop_rbracket=*/false,
                                          /*stop_rparen=*/false,
                                          depth);
            if (field.value.empty())
                lex_.error("expected expression after ':'", t.line, t.col);
        }

        return field;
    }

    std::unique_ptr<ArrayLiteral> parse_array_literal(int depth) {
        Token lbracket = lex_.next();
        assert(lbracket.type == TokenType::LBracket);
        check_depth(depth, lbracket.line, lbracket.col);

        auto arr = std::make_unique<ArrayLiteral>();

        while (true) {
            Token t = lex_.peek();
            if (t.type == TokenType::RBracket) { lex_.next(); break; }
            if (t.type == TokenType::Eof)
                lex_.error("unterminated '['", lbracket.line, lbracket.col);

            auto elem = parse_sql_parts(/*stop_comma=*/true,
                                        /*stop_rbrace=*/false,
                                        /*stop_rbracket=*/true,
                                        /*stop_rparen=*/false,
                                        depth);
            if (elem.empty())
                lex_.error("expected expression in array literal");
            arr->elements.push_back(std::move(elem));

            t = lex_.peek();
            if (t.type == TokenType::Comma) {
                lex_.next();
            } else if (t.type != TokenType::RBracket) {
                lex_.error("expected ',' or ']' in array literal");
            }
        }

        return arr;
    }

    Lexer& lex_;
    std::unordered_map<std::string, std::string> alias_map_;
};

// ── Renderer ────────────────────────────────────────────────────────

// Escape single-quote characters for use inside a SQL string literal.
static std::string sql_escape_key(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c == '\'') r += "''";
        else r += c;
    }
    return r;
}

class Renderer {
public:
    std::string render_document(const SqlParts& parts) {
        std::string out;
        render_parts(parts, out, /*nested=*/false);
        return out;
    }

private:
    void render_parts(const SqlParts& parts, std::string& out, bool nested) {
        for (const auto& part : parts) {
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    out += v;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<DeepSelect>>) {
                    render_deep_select(*v, out, nested);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ObjectLiteral>>) {
                    render_object(*v, out);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ArrayLiteral>>) {
                    render_array(*v, out);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<AutoJoin>>) {
                    render_auto_join(*v, out);
                }
            }, part);
        }
    }

    void render_deep_select(const DeepSelect& ds, std::string& out, bool nested) {
        if (nested) out += "(";
        out += "SELECT ";

        bool is_object = std::holds_alternative<ObjectLiteral>(ds.projection);

        if (nested) out += "json_group_array(";

        if (is_object) {
            render_object(std::get<ObjectLiteral>(ds.projection), out);
        } else {
            const auto& arr = std::get<ArrayLiteral>(ds.projection);
            if (arr.elements.size() == 1) {
                if (!nested) out += "json_group_array(";
                render_parts(arr.elements[0], out, /*nested=*/true);
                if (!nested) out += ")";
            } else {
                if (!nested) out += "json_group_array(";
                render_array(arr, out);
                if (!nested) out += ")";
            }
        }

        if (nested) out += ")";

        if (!ds.tail.empty()) {
            out += " ";
            render_parts(ds.tail, out, /*nested=*/true);
        }

        if (nested) out += ")";
    }

    void render_object(const ObjectLiteral& obj, std::string& out) {
        out += "json_object(";
        for (size_t i = 0; i < obj.fields.size(); ++i) {
            if (i > 0) out += ", ";
            const auto& f = obj.fields[i];
            out += "'";
            out += sql_escape_key(f.key);
            out += "', ";
            if (f.value.empty()) {
                out += f.key;
            } else {
                render_parts(f.value, out, /*nested=*/true);
            }
        }
        out += ")";
    }

    void render_array(const ArrayLiteral& arr, std::string& out) {
        out += "json_array(";
        for (size_t i = 0; i < arr.elements.size(); ++i) {
            if (i > 0) out += ", ";
            render_parts(arr.elements[i], out, /*nested=*/true);
        }
        out += ")";
    }

    void render_auto_join(const AutoJoin& aj, std::string& out) {
        out += aj.child_table;
        const auto& child_ref =
            aj.child_alias.empty() ? aj.child_table : aj.child_alias;
        if (!aj.child_alias.empty()) {
            out += " ";
            out += aj.child_alias;
        }
        out += " WHERE ";
        out += child_ref;
        out += ".";
        out += aj.parent_table;
        out += "_id = ";
        out += aj.parent_alias;
        out += ".";
        out += aj.parent_table;
        out += "_id";
    }
};

} // anonymous namespace

// ── Public API ──────────────────────────────────────────────────────

std::string transpile(const std::string& input) {
    auto alias_map = build_alias_map(input);
    Lexer lex(input);
    Parser parser(lex, std::move(alias_map));
    SqlParts doc = parser.parse_document();
    Renderer renderer;
    return renderer.render_document(doc);
}

} // namespace sqldeep

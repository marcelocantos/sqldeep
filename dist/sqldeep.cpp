// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// sqldeep — JSON5-like SQL syntax transpiler.
//
// Implementation strategy: parse the input via deepparser (which knows
// the sqldeep grammar), then walk the resulting AST and rewrite every
// sqldeep-extension node in place into standard SQL nodes. Once the
// AST contains only plain SQL kinds, deepparser's canonical unparser
// emits the final SQL text. Sqldeep itself owns nothing about parsing
// or printing — it is purely an AST-to-AST transformer plus the
// outward-facing C API.

#include "sqldeep.h"

extern "C" {
#include "liteparser.h"
#include "arena.h"
}

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sqldeep {

struct ForeignKey {
    std::string from_table;
    std::string to_table;
    struct ColumnPair {
        std::string from_column;
        std::string to_column;
    };
    std::vector<ColumnPair> columns;
};

enum class Backend { sqlite, postgres, sqlite_vanilla };

class Error : public std::runtime_error {
public:
    Error(const std::string& msg, int line, int col)
        : std::runtime_error(msg), line_(line), col_(col) {}
    int line() const { return line_; }
    int col()  const { return col_; }
private:
    int line_;
    int col_;
};

namespace {

// ── FK index ────────────────────────────────────────────────────────

using FkIndex = std::map<std::pair<std::string,std::string>,
                         std::vector<const ForeignKey*>>;

FkIndex build_fk_index(const std::vector<ForeignKey>& fks) {
    FkIndex idx;
    for (const auto& fk : fks) {
        idx[{fk.from_table, fk.to_table}].push_back(&fk);
    }
    return idx;
}

// Convention mode (fk_index == nullptr): child has FK '<parent>_id'.
std::vector<std::pair<std::string,std::string>>
resolve_fk_columns(const std::string& child_table,
                   const std::string& parent_table,
                   const FkIndex* fk_index) {
    if (!fk_index) {
        std::string col = parent_table + "_id";
        return {{col, col}};
    }
    auto it = fk_index->find({child_table, parent_table});
    if (it == fk_index->end() || it->second.empty()) {
        throw Error("no foreign key from '" + child_table + "' to '" +
                    parent_table + "'", 0, 0);
    }
    if (it->second.size() > 1) {
        throw Error("ambiguous foreign key from '" + child_table + "' to '" +
                    parent_table + "' (" + std::to_string(it->second.size()) +
                    " candidates)", 0, 0);
    }
    std::vector<std::pair<std::string,std::string>> cols;
    cols.reserve(it->second[0]->columns.size());
    for (const auto& cp : it->second[0]->columns)
        cols.emplace_back(cp.from_column, cp.to_column);
    return cols;
}

// ── AST construction helpers ─────────────────────────────────────────
//
// Sqldeep mutates the deepparser AST in place and also constructs new
// LpNode objects for replacement subtrees. The helpers below build the
// standard-SQL node kinds we emit; they parallel deepparser's
// grammar-action factories but live outside the parser since we're not
// parsing — we have nothing but an arena to allocate into.

LpNode *new_node(arena_t *arena, LpNodeKind kind) {
    auto *n = static_cast<LpNode*>(arena_zeroalloc(arena, sizeof(LpNode)));
    if (n) n->kind = kind;
    return n;
}

char *arena_str(arena_t *arena, const std::string& s) {
    return arena_strdup(arena, s.c_str());
}

void list_append(arena_t *arena, LpNodeList *list, LpNode *item) {
    if (list->count >= list->capacity) {
        int cap = list->capacity ? list->capacity * 2 : 4;
        auto **items = static_cast<LpNode**>(
            arena_alloc(arena, sizeof(LpNode*) * cap));
        if (!items) return;
        if (list->items)
            std::memcpy(items, list->items, sizeof(LpNode*) * list->count);
        list->items = items;
        list->capacity = cap;
    }
    list->items[list->count++] = item;
}

LpNode *make_string_lit(arena_t *arena, const std::string& value) {
    auto *n = new_node(arena, LP_EXPR_LITERAL_STRING);
    if (n) n->u.literal.value = arena_str(arena, value);
    return n;
}

LpNode *make_int_lit(arena_t *arena, const std::string& digits) {
    auto *n = new_node(arena, LP_EXPR_LITERAL_INT);
    if (n) n->u.literal.value = arena_str(arena, digits);
    return n;
}

LpNode *make_column_ref(arena_t *arena, const std::string& column) {
    auto *n = new_node(arena, LP_EXPR_COLUMN_REF);
    if (n) n->u.column_ref.column = arena_str(arena, column);
    return n;
}

LpNode *make_function(arena_t *arena, const std::string& name,
                       std::vector<LpNode*> args) {
    auto *n = new_node(arena, LP_EXPR_FUNCTION);
    if (!n) return nullptr;
    n->u.function.name = arena_str(arena, name);
    for (auto *a : args) list_append(arena, &n->u.function.args, a);
    return n;
}

LpNode *make_cast(arena_t *arena, LpNode *expr, const std::string& type_name) {
    auto *n = new_node(arena, LP_EXPR_CAST);
    if (!n) return nullptr;
    n->u.cast.expr = expr;
    n->u.cast.type_name = arena_str(arena, type_name);
    return n;
}

LpNode *make_limit(arena_t *arena, int count) {
    auto *n = new_node(arena, LP_LIMIT);
    if (!n) return nullptr;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", count);
    n->u.limit.count = make_int_lit(arena, buf);
    return n;
}

// ── Transformer ─────────────────────────────────────────────────────

constexpr int kMaxNestingDepth = 200;

class Transformer {
public:
    Transformer(arena_t *arena, Backend backend, const FkIndex *fk_index)
        : arena_(arena), backend_(backend), fk_index_(fk_index) {}

    // Mutate the AST in place. Returns the (possibly new) root node.
    LpNode *transform(LpNode *node) {
        return walk(node, /*depth=*/0);
    }

private:
    arena_t      *arena_;
    Backend       backend_;
    const FkIndex *fk_index_;

    // Dialect function names ----------------------------------------

    const char *fn_object() const {
        switch (backend_) {
            case Backend::postgres:       return "jsonb_build_object";
            case Backend::sqlite_vanilla: return "json_object";
            default:                      return "sqldeep_json_object";
        }
    }
    const char *fn_array() const {
        switch (backend_) {
            case Backend::postgres:       return "jsonb_build_array";
            case Backend::sqlite_vanilla: return "json_array";
            default:                      return "sqldeep_json_array";
        }
    }
    const char *fn_group_array() const {
        switch (backend_) {
            case Backend::postgres:       return "jsonb_agg";
            case Backend::sqlite_vanilla: return "json_group_array";
            default:                      return "sqldeep_json_group_array";
        }
    }

    // ── Walker ────────────────────────────────────────────────────
    //
    // Visit every node in post-order: children first, then the node
    // itself. By the time we rewrite a sqldeep node, its children are
    // already plain-SQL AST.

    LpNode *walk(LpNode *node, int depth) {
        if (!node) return nullptr;
        if (depth > kMaxNestingDepth)
            throw Error("maximum nesting depth exceeded", 0, 0);

        walk_children(node, depth + 1);

        switch (node->kind) {
            case LP_EXPR_SQLDEEP_OBJECT:    return rewrite_object(node);
            case LP_EXPR_SQLDEEP_ARRAY:     return rewrite_array(node);
            case LP_EXPR_SQLDEEP_JSON_PATH: return rewrite_json_path(node);
            case LP_STMT_SELECT:            return rewrite_select(node);
            default:                        return node;
        }
    }

    // Recurse into every child slot that can contain an LpNode or an
    // LpNodeList. This is the bulk of the walker — every node kind
    // that can contain an expression or statement child must list
    // those children here. Anything that's a leaf (literals, naked
    // column refs, identifiers) has nothing to do.

    void walk_children(LpNode *node, int depth) {
        switch (node->kind) {
            case LP_STMT_SELECT:
                walk_list(&node->u.select.result_columns, depth);
                node->u.select.from   = walk(node->u.select.from, depth);
                node->u.select.where  = walk(node->u.select.where, depth);
                walk_list(&node->u.select.group_by, depth);
                node->u.select.having = walk(node->u.select.having, depth);
                walk_list(&node->u.select.order_by, depth);
                node->u.select.limit  = walk(node->u.select.limit, depth);
                walk_list(&node->u.select.window_defs, depth);
                node->u.select.with   = walk(node->u.select.with, depth);
                break;
            case LP_COMPOUND_SELECT:
                node->u.compound.left  = walk(node->u.compound.left, depth);
                node->u.compound.right = walk(node->u.compound.right, depth);
                break;
            case LP_RESULT_COLUMN:
                node->u.result_column.expr =
                    walk(node->u.result_column.expr, depth);
                break;
            case LP_EXPR_BINARY_OP:
                node->u.binary.left  = walk(node->u.binary.left, depth);
                node->u.binary.right = walk(node->u.binary.right, depth);
                break;
            case LP_EXPR_UNARY_OP:
                node->u.unary.operand = walk(node->u.unary.operand, depth);
                break;
            case LP_EXPR_FUNCTION:
                walk_list(&node->u.function.args, depth);
                walk_list(&node->u.function.order_by, depth);
                node->u.function.filter = walk(node->u.function.filter, depth);
                node->u.function.over   = walk(node->u.function.over, depth);
                break;
            case LP_EXPR_CAST:
                node->u.cast.expr = walk(node->u.cast.expr, depth);
                break;
            case LP_EXPR_COLLATE:
                node->u.collate.expr = walk(node->u.collate.expr, depth);
                break;
            case LP_EXPR_BETWEEN:
                node->u.between.expr = walk(node->u.between.expr, depth);
                node->u.between.low  = walk(node->u.between.low, depth);
                node->u.between.high = walk(node->u.between.high, depth);
                break;
            case LP_EXPR_IN:
                node->u.in.expr   = walk(node->u.in.expr, depth);
                node->u.in.select = walk(node->u.in.select, depth);
                walk_list(&node->u.in.values, depth);
                break;
            case LP_EXPR_EXISTS:
                node->u.exists.select = walk(node->u.exists.select, depth);
                break;
            case LP_EXPR_SUBQUERY:
                node->u.subquery.select = walk(node->u.subquery.select, depth);
                break;
            case LP_EXPR_CASE:
                node->u.case_.operand = walk(node->u.case_.operand, depth);
                walk_list(&node->u.case_.when_exprs, depth);
                node->u.case_.else_expr = walk(node->u.case_.else_expr, depth);
                break;
            case LP_FROM_SUBQUERY:
                node->u.from_subquery.select =
                    walk(node->u.from_subquery.select, depth);
                break;
            case LP_JOIN_CLAUSE:
                node->u.join.left    = walk(node->u.join.left, depth);
                node->u.join.right   = walk(node->u.join.right, depth);
                node->u.join.on_expr = walk(node->u.join.on_expr, depth);
                break;
            case LP_ORDER_TERM:
                node->u.order_term.expr =
                    walk(node->u.order_term.expr, depth);
                break;
            case LP_LIMIT:
                node->u.limit.count  = walk(node->u.limit.count, depth);
                node->u.limit.offset = walk(node->u.limit.offset, depth);
                break;
            case LP_EXPR_SQLDEEP_OBJECT:
                walk_list(&node->u.sqldeep_object.fields, depth);
                break;
            case LP_SQLDEEP_FIELD:
                node->u.sqldeep_field.key_expr =
                    walk(node->u.sqldeep_field.key_expr, depth);
                node->u.sqldeep_field.value =
                    walk(node->u.sqldeep_field.value, depth);
                break;
            case LP_EXPR_SQLDEEP_ARRAY:
                walk_list(&node->u.sqldeep_array.elements, depth);
                break;
            case LP_EXPR_SQLDEEP_JSON_PATH:
                node->u.sqldeep_json_path.base =
                    walk(node->u.sqldeep_json_path.base, depth);
                break;
            default:
                break;  // leaves and not-yet-handled cases
        }
    }

    void walk_list(LpNodeList *list, int depth) {
        if (!list) return;
        for (int i = 0; i < list->count; i++)
            list->items[i] = walk(list->items[i], depth);
    }

    // ── Rewrites ─────────────────────────────────────────────────

    // { a, key: expr, "k": v, (e): v, a.b } →
    //   fn_object('a', a, 'key', expr, 'k', v, e, v, 'b', a.b)
    LpNode *rewrite_object(LpNode *node) {
        std::vector<LpNode*> args;
        const auto& fields = node->u.sqldeep_object.fields;
        for (int i = 0; i < fields.count; i++) {
            LpNode *f = fields.items[i];
            if (!f) continue;
            const auto& sf = f->u.sqldeep_field;
            switch (sf.key_form) {
                case 0:  // bare
                    args.push_back(make_string_lit(arena_, sf.key_text));
                    args.push_back(make_column_ref(arena_, sf.key_text));
                    break;
                case 1:  // named id : expr
                case 2:  // "string" : expr
                case 5:  // qualified  a.b
                    args.push_back(make_string_lit(arena_, sf.key_text));
                    args.push_back(sf.value);
                    break;
                case 3:  // (expr) : val
                    args.push_back(sf.key_expr);
                    args.push_back(sf.value);
                    break;
                case 4:  // recursive children — handled by RECURSE expansion
                default:
                    break;
            }
        }
        // Mutate node in place to LP_EXPR_FUNCTION.
        node->kind = LP_EXPR_FUNCTION;
        std::memset(&node->u, 0, sizeof(node->u));
        node->u.function.name = arena_strdup(arena_, fn_object());
        for (auto *a : args) list_append(arena_, &node->u.function.args, a);
        return node;
    }

    // [ e1, e2, ... ] → fn_array(e1, e2, ...)
    LpNode *rewrite_array(LpNode *node) {
        LpNodeList elements = node->u.sqldeep_array.elements;
        node->kind = LP_EXPR_FUNCTION;
        std::memset(&node->u, 0, sizeof(node->u));
        node->u.function.name = arena_strdup(arena_, fn_array());
        for (int i = 0; i < elements.count; i++)
            list_append(arena_, &node->u.function.args, elements.items[i]);
        return node;
    }

    // (base).a.b[0] →
    //   SQLite/Vanilla: json_extract(CAST((base) AS TEXT), '$.a.b[0]')
    //   Postgres:       jsonb_extract_path(base, 'a', 'b', '0')
    LpNode *rewrite_json_path(LpNode *node) {
        LpNode *base = node->u.sqldeep_json_path.base;
        LpNodeList segs = node->u.sqldeep_json_path.segments;

        if (backend_ == Backend::postgres) {
            std::vector<LpNode*> args;
            args.push_back(base);
            for (int i = 0; i < segs.count; i++) {
                LpNode *seg = segs.items[i];
                if (!seg) continue;
                args.push_back(make_string_lit(arena_,
                    seg->u.literal.value ? seg->u.literal.value : ""));
            }
            node->kind = LP_EXPR_FUNCTION;
            std::memset(&node->u, 0, sizeof(node->u));
            node->u.function.name = arena_strdup(arena_, "jsonb_extract_path");
            for (auto *a : args) list_append(arena_, &node->u.function.args, a);
            return node;
        }

        // Build "$.a.b[0]" path string.
        std::string path = "$";
        for (int i = 0; i < segs.count; i++) {
            LpNode *seg = segs.items[i];
            if (!seg) continue;
            const char *v = seg->u.literal.value ? seg->u.literal.value : "";
            if (seg->kind == LP_EXPR_LITERAL_INT) {
                path += "[";
                path += v;
                path += "]";
            } else {
                path += ".";
                path += v;
            }
        }

        LpNode *cast = make_cast(arena_, base, "TEXT");
        LpNode *path_lit = make_string_lit(arena_, path);

        node->kind = LP_EXPR_FUNCTION;
        std::memset(&node->u, 0, sizeof(node->u));
        node->u.function.name = arena_strdup(arena_, "json_extract");
        list_append(arena_, &node->u.function.args, cast);
        list_append(arena_, &node->u.function.args, path_lit);
        return node;
    }

    // SELECT-level handling: singular → LIMIT 1; deep projection wrapping;
    // FROM-first flag cleared (canonical unparser emits standard order).
    LpNode *rewrite_select(LpNode *node) {
        // The from_first flag was a parser convenience; the unparser
        // does NOT emit FROM-first in canonical output. Clear it so
        // the output reads as standard SQL.
        node->u.select.sqldeep_from_first = 0;

        // SELECT/1 → LIMIT 1 (unless a LIMIT is already specified).
        if (node->u.select.sqldeep_singular) {
            node->u.select.sqldeep_singular = 0;
            if (!node->u.select.limit) {
                node->u.select.limit = make_limit(arena_, 1);
            }
        }

        // Deep-projection wrapping: the SELECT's deep projection
        // (now rewritten to a fn_object/fn_array call) gets wrapped in
        // fn_group_array when the SELECT is a *deep* value subquery —
        // i.e. it sits where one row's projection becomes one
        // collection-element of an enclosing deep projection. Concretely:
        // the SELECT's parent is LP_EXPR_SUBQUERY, and that subquery's
        // parent is LP_SQLDEEP_FIELD or LP_EXPR_SQLDEEP_ARRAY (the
        // sqldeep containers that supply a value slot).
        //
        // Plain scalar-subquery contexts (function args, WHERE,
        // arithmetic, IN, EXISTS, etc.) do NOT wrap — the inner SELECT
        // already returns a single composite value.
        LpNode *parent = node->parent;
        if (!parent || parent->kind != LP_EXPR_SUBQUERY) return node;
        LpNode *grandparent = parent->parent;
        if (!grandparent) return node;
        if (grandparent->kind != LP_SQLDEEP_FIELD &&
            grandparent->kind != LP_EXPR_SQLDEEP_ARRAY) return node;
        if (node->u.select.result_columns.count != 1) return node;

        LpNode *rc = node->u.select.result_columns.items[0];
        LpNode *proj = (rc && rc->kind == LP_RESULT_COLUMN)
                          ? rc->u.result_column.expr : rc;
        if (!proj) return node;

        // Skip wrapping when the inner SELECT is singular — it already
        // got LIMIT 1, the value is one row, no group_array.
        // (Tracked via the just-set LIMIT — see above.)

        // The projection function-call name tells us what shape it has.
        // We only wrap our own builders, never user code.
        if (proj->kind != LP_EXPR_FUNCTION) return node;
        const char *pname = proj->u.function.name;
        if (!pname) return node;

        const char *obj  = fn_object();
        const char *arr  = fn_array();
        bool is_obj = std::strcmp(pname, obj) == 0;
        bool is_arr = std::strcmp(pname, arr) == 0;
        if (!is_obj && !is_arr) return node;

        // Singular projections: leave unwrapped (LIMIT 1 set earlier).
        if (node->u.select.limit) {
            // If the LIMIT was set by our singular promotion above, the
            // sqldeep_singular flag was cleared. We can't distinguish
            // "user wrote LIMIT 1" from "we set it" — but treating both
            // as singular for wrap purposes is correct: a LIMIT-1
            // subquery returns at most one row, so json_group_array
            // would just wrap one element, semantically equivalent but
            // a needless wrap. Prefer leaving unwrapped here. (Sqldeep
            // hand-written renderer makes the same call.)
            return node;
        }

        // Single-element array projection: hoist the element out of
        // the json_array call before wrapping. [expr] becomes
        // group_array(expr), not group_array(json_array(expr)).
        if (is_arr && proj->u.function.args.count == 1) {
            LpNode *elem = proj->u.function.args.items[0];
            LpNode *wrapped = make_function(arena_, fn_group_array(), {elem});
            if (rc && rc->kind == LP_RESULT_COLUMN)
                rc->u.result_column.expr = wrapped;
            else
                node->u.select.result_columns.items[0] = wrapped;
            return node;
        }

        LpNode *wrapped = make_function(arena_, fn_group_array(), {proj});
        if (rc && rc->kind == LP_RESULT_COLUMN)
            rc->u.result_column.expr = wrapped;
        else
            node->u.select.result_columns.items[0] = wrapped;
        return node;
    }
};

// ── End-to-end pipeline ──────────────────────────────────────────────

std::string transpile_impl(const std::string& input,
                            const std::vector<ForeignKey>* fks,
                            Backend backend) {
    arena_t *arena = arena_create(64 * 1024);
    if (!arena) throw Error("out of memory", 0, 0);

    const char *parse_err = nullptr;
    LpNodeList *stmts = lp_parse_all(input.c_str(), arena, &parse_err);
    if (!stmts) {
        std::string msg = parse_err ? parse_err : "parse error";
        arena_destroy(arena);
        throw Error(msg, 0, 0);
    }

    FkIndex fk_index;
    if (fks) fk_index = build_fk_index(*fks);

    try {
        Transformer t(arena, backend, fks ? &fk_index : nullptr);
        for (int i = 0; i < stmts->count; i++) {
            stmts->items[i] = t.transform(stmts->items[i]);
            // Re-fix parent pointers since some children may have been
            // mutated and new nodes inserted.
            if (stmts->items[i]) lp_fix_parents(stmts->items[i]);
        }
    } catch (...) {
        arena_destroy(arena);
        throw;
    }

    // Stitch all statements together separated by "; ".
    std::string out;
    for (int i = 0; i < stmts->count; i++) {
        if (i > 0) out += "; ";
        char *sql = lp_ast_to_sql(stmts->items[i], arena);
        if (sql) out += sql;
    }

    arena_destroy(arena);
    return out;
}

} // namespace

// ── Public C++ API ──────────────────────────────────────────────────

std::string transpile(const std::string& input) {
    return transpile_impl(input, nullptr, Backend::sqlite);
}

std::string transpile(const std::string& input,
                       const std::vector<ForeignKey>& fks) {
    return transpile_impl(input, &fks, Backend::sqlite);
}

std::string transpile(const std::string& input, Backend backend) {
    return transpile_impl(input, nullptr, backend);
}

std::string transpile(const std::string& input,
                       const std::vector<ForeignKey>& fks,
                       Backend backend) {
    return transpile_impl(input, &fks, backend);
}

} // namespace sqldeep

// ── C API bridge ────────────────────────────────────────────────────

namespace {

char *dup_str(const std::string& s) {
    char *p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

void set_error(char** err_msg, int* err_line, int* err_col,
               const sqldeep::Error& e) {
    if (err_msg)  *err_msg = dup_str(e.what());
    if (err_line) *err_line = e.line();
    if (err_col)  *err_col = e.col();
}

void clear_error(char** err_msg, int* err_line, int* err_col) {
    if (err_msg)  *err_msg = nullptr;
    if (err_line) *err_line = 0;
    if (err_col)  *err_col = 0;
}

sqldeep::Backend to_backend(sqldeep_backend b) {
    switch (b) {
        case SQLDEEP_POSTGRES:       return sqldeep::Backend::postgres;
        case SQLDEEP_SQLITE_VANILLA: return sqldeep::Backend::sqlite_vanilla;
        default:                     return sqldeep::Backend::sqlite;
    }
}

std::vector<sqldeep::ForeignKey> to_cpp_fks(const sqldeep_foreign_key* fks,
                                              int fk_count) {
    std::vector<sqldeep::ForeignKey> cpp_fks;
    cpp_fks.reserve(fk_count);
    for (int i = 0; i < fk_count; ++i) {
        sqldeep::ForeignKey fk;
        fk.from_table = fks[i].from_table;
        fk.to_table   = fks[i].to_table;
        fk.columns.reserve(fks[i].column_count);
        for (int j = 0; j < fks[i].column_count; ++j) {
            fk.columns.push_back({
                fks[i].columns[j].from_column,
                fks[i].columns[j].to_column,
            });
        }
        cpp_fks.push_back(std::move(fk));
    }
    return cpp_fks;
}

} // namespace

extern "C" {

char* sqldeep_transpile(const char* input,
                        char** err_msg, int* err_line, int* err_col) {
    return sqldeep_transpile_backend(input, SQLDEEP_SQLITE,
                                      err_msg, err_line, err_col);
}

char* sqldeep_transpile_fk(const char* input,
                           const sqldeep_foreign_key* fks, int fk_count,
                           char** err_msg, int* err_line, int* err_col) {
    return sqldeep_transpile_fk_backend(input, SQLDEEP_SQLITE, fks, fk_count,
                                         err_msg, err_line, err_col);
}

char* sqldeep_transpile_backend(const char* input,
                                sqldeep_backend backend,
                                char** err_msg, int* err_line, int* err_col) {
    clear_error(err_msg, err_line, err_col);
    try {
        return dup_str(sqldeep::transpile(input, to_backend(backend)));
    } catch (const sqldeep::Error& e) {
        set_error(err_msg, err_line, err_col, e);
        return nullptr;
    }
}

char* sqldeep_transpile_fk_backend(const char* input,
                                   sqldeep_backend backend,
                                   const sqldeep_foreign_key* fks, int fk_count,
                                   char** err_msg, int* err_line, int* err_col) {
    clear_error(err_msg, err_line, err_col);
    try {
        auto cpp_fks = to_cpp_fks(fks, fk_count);
        return dup_str(sqldeep::transpile(input, cpp_fks, to_backend(backend)));
    } catch (const sqldeep::Error& e) {
        set_error(err_msg, err_line, err_col, e);
        return nullptr;
    }
}

const char* sqldeep_version(void) {
    return SQLDEEP_VERSION;
}

void sqldeep_free(void* ptr) {
    std::free(ptr);
}

} // extern "C"

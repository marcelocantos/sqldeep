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

// XML emission flavour. JSX and JSONML are sqldeep transpiler macros
// — `jsx(<el/>)` / `jsonml(<el/>)` — that select alternative XML
// helper function names (xml_element_jsx, xml_attrs_jsx, jsx_agg vs.
// the plain xml_* family).
enum class XmlMode { Xml, Jsx, Jsonml };

class Transformer {
public:
    Transformer(arena_t *arena, Backend backend, const FkIndex *fk_index)
        : arena_(arena), backend_(backend), fk_index_(fk_index) {}

    // Mutate the AST in place. Returns the (possibly new) root node.
    LpNode *transform(LpNode *node) {
        return walk(node, /*depth=*/0, XmlMode::Xml);
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
    // BLOB-protocol wrapper used to inject a typed JSON value into a
    // JSON-building call so booleans (and only booleans, currently)
    // round-trip as `true` / `false` rather than `1` / `0`. Postgres
    // jsonb already has a native bool, so no wrapper is needed there.
    const char *fn_json_blob() const {
        switch (backend_) {
            case Backend::postgres:       return nullptr;
            case Backend::sqlite_vanilla: return "json";
            default:                      return "sqldeep_json";
        }
    }

    // Detect a bare `true` / `false` literal used as a value. SQLite's
    // grammar doesn't have boolean keywords — `true`/`false` parse as
    // identifier column refs (with no table qualifier). Returns the
    // canonical lower-case form, or NULL if `expr` isn't one of those.
    static const char *bool_literal_text(LpNode *expr) {
        if (!expr) return nullptr;
        const char *name = nullptr;
        if (expr->kind == LP_EXPR_LITERAL_BOOL) {
            name = expr->u.literal.value;
        } else if (expr->kind == LP_EXPR_COLUMN_REF
                && !expr->u.column_ref.schema
                && !expr->u.column_ref.table) {
            name = expr->u.column_ref.column;
        }
        if (!name) return nullptr;
        if ((name[0] == 't' || name[0] == 'T')
            && (name[1] == 'r' || name[1] == 'R')
            && (name[2] == 'u' || name[2] == 'U')
            && (name[3] == 'e' || name[3] == 'E')
            && name[4] == '\0')
            return "true";
        if ((name[0] == 'f' || name[0] == 'F')
            && (name[1] == 'a' || name[1] == 'A')
            && (name[2] == 'l' || name[2] == 'L')
            && (name[3] == 's' || name[3] == 'S')
            && (name[4] == 'e' || name[4] == 'E')
            && name[5] == '\0')
            return "false";
        return nullptr;
    }

    // Wrap a bare `true` / `false` literal in fn_json('true') /
    // fn_json('false') when it appears in a JSON value slot — object
    // field value, array element, or (later) XML attribute value.
    // No wrap in Postgres mode (jsonb has native bool).
    LpNode *wrap_json_bool(LpNode *expr) {
        const char *lit = bool_literal_text(expr);
        if (!lit) return expr;
        const char *fn = fn_json_blob();
        if (!fn) return expr;
        return make_function(arena_, fn, {make_string_lit(arena_, lit)});
    }

    // ── Walker ────────────────────────────────────────────────────
    //
    // Visit every node in post-order: children first, then the node
    // itself. By the time we rewrite a sqldeep node, its children are
    // already plain-SQL AST.

    LpNode *walk(LpNode *node, int depth, XmlMode mode) {
        if (!node) return nullptr;
        if (depth > kMaxNestingDepth)
            throw Error("maximum nesting depth exceeded", 0, 0);

        // jsx(<el/>) / jsonml(<el/>) transpiler-macro wrapper: absorb
        // the outer function call and switch the inner XML element's
        // emission mode. Detected pre-recursion so the mode propagates
        // down through nested XML children.
        if (node->kind == LP_EXPR_FUNCTION
            && node->u.function.name
            && node->u.function.args.count == 1
            && node->u.function.args.items[0]
            && node->u.function.args.items[0]->kind == LP_EXPR_SQLDEEP_XML) {
            XmlMode m = XmlMode::Xml;
            const char *fn = node->u.function.name;
            if (std::strcmp(fn, "jsx") == 0)        m = XmlMode::Jsx;
            else if (std::strcmp(fn, "jsonml") == 0) m = XmlMode::Jsonml;
            if (m != XmlMode::Xml) {
                LpNode *xml = node->u.function.args.items[0];
                walk_xml_children(xml, depth + 1, m);
                return rewrite_xml(xml, m);
            }
        }

        walk_children(node, depth + 1, mode);

        switch (node->kind) {
            case LP_EXPR_SQLDEEP_OBJECT:    return rewrite_object(node);
            case LP_EXPR_SQLDEEP_ARRAY:     return rewrite_array(node);
            case LP_EXPR_SQLDEEP_JSON_PATH: return rewrite_json_path(node);
            case LP_EXPR_SQLDEEP_XML:       return rewrite_xml(node, mode);
            case LP_STMT_SELECT:            return rewrite_select(node);
            default:                        return node;
        }
    }

    // Recurse into every child slot that can contain an LpNode or an
    // LpNodeList. This is the bulk of the walker — every node kind
    // that can contain an expression or statement child must list
    // those children here. Anything that's a leaf (literals, naked
    // column refs, identifiers) has nothing to do.

    void walk_children(LpNode *node, int depth, XmlMode mode) {
        switch (node->kind) {
            case LP_STMT_SELECT:
                walk_list(&node->u.select.result_columns, depth, mode);
                node->u.select.from   = walk(node->u.select.from, depth, mode);
                node->u.select.where  = walk(node->u.select.where, depth, mode);
                walk_list(&node->u.select.group_by, depth, mode);
                node->u.select.having = walk(node->u.select.having, depth, mode);
                walk_list(&node->u.select.order_by, depth, mode);
                node->u.select.limit  = walk(node->u.select.limit, depth, mode);
                walk_list(&node->u.select.window_defs, depth, mode);
                node->u.select.with   = walk(node->u.select.with, depth, mode);
                break;
            case LP_COMPOUND_SELECT:
                node->u.compound.left  = walk(node->u.compound.left, depth, mode);
                node->u.compound.right = walk(node->u.compound.right, depth, mode);
                break;
            case LP_RESULT_COLUMN:
                node->u.result_column.expr =
                    walk(node->u.result_column.expr, depth, mode);
                break;
            case LP_EXPR_BINARY_OP:
                node->u.binary.left  = walk(node->u.binary.left, depth, mode);
                node->u.binary.right = walk(node->u.binary.right, depth, mode);
                break;
            case LP_EXPR_UNARY_OP:
                node->u.unary.operand = walk(node->u.unary.operand, depth, mode);
                break;
            case LP_EXPR_FUNCTION:
                walk_list(&node->u.function.args, depth, mode);
                walk_list(&node->u.function.order_by, depth, mode);
                node->u.function.filter = walk(node->u.function.filter, depth, mode);
                node->u.function.over   = walk(node->u.function.over, depth, mode);
                break;
            case LP_EXPR_CAST:
                node->u.cast.expr = walk(node->u.cast.expr, depth, mode);
                break;
            case LP_EXPR_COLLATE:
                node->u.collate.expr = walk(node->u.collate.expr, depth, mode);
                break;
            case LP_EXPR_BETWEEN:
                node->u.between.expr = walk(node->u.between.expr, depth, mode);
                node->u.between.low  = walk(node->u.between.low, depth, mode);
                node->u.between.high = walk(node->u.between.high, depth, mode);
                break;
            case LP_EXPR_IN:
                node->u.in.expr   = walk(node->u.in.expr, depth, mode);
                node->u.in.select = walk(node->u.in.select, depth, mode);
                walk_list(&node->u.in.values, depth, mode);
                break;
            case LP_EXPR_EXISTS:
                node->u.exists.select = walk(node->u.exists.select, depth, mode);
                break;
            case LP_EXPR_SUBQUERY:
                node->u.subquery.select = walk(node->u.subquery.select, depth, mode);
                break;
            case LP_EXPR_CASE:
                node->u.case_.operand = walk(node->u.case_.operand, depth, mode);
                walk_list(&node->u.case_.when_exprs, depth, mode);
                node->u.case_.else_expr = walk(node->u.case_.else_expr, depth, mode);
                break;
            case LP_FROM_SUBQUERY:
                node->u.from_subquery.select =
                    walk(node->u.from_subquery.select, depth, mode);
                break;
            case LP_JOIN_CLAUSE:
                node->u.join.left    = walk(node->u.join.left, depth, mode);
                node->u.join.right   = walk(node->u.join.right, depth, mode);
                node->u.join.on_expr = walk(node->u.join.on_expr, depth, mode);
                break;
            case LP_ORDER_TERM:
                node->u.order_term.expr =
                    walk(node->u.order_term.expr, depth, mode);
                break;
            case LP_LIMIT:
                node->u.limit.count  = walk(node->u.limit.count, depth, mode);
                node->u.limit.offset = walk(node->u.limit.offset, depth, mode);
                break;
            case LP_EXPR_SQLDEEP_OBJECT:
                walk_list(&node->u.sqldeep_object.fields, depth, mode);
                break;
            case LP_SQLDEEP_FIELD:
                node->u.sqldeep_field.key_expr =
                    walk(node->u.sqldeep_field.key_expr, depth, mode);
                node->u.sqldeep_field.value =
                    walk(node->u.sqldeep_field.value, depth, mode);
                break;
            case LP_EXPR_SQLDEEP_ARRAY:
                walk_list(&node->u.sqldeep_array.elements, depth, mode);
                break;
            case LP_EXPR_SQLDEEP_JSON_PATH:
                node->u.sqldeep_json_path.base =
                    walk(node->u.sqldeep_json_path.base, depth, mode);
                break;
            case LP_EXPR_SQLDEEP_XML:
                walk_xml_children(node, depth, mode);
                break;
            case LP_STMT_CREATE_VIEW:
                node->u.create_view.select =
                    walk(node->u.create_view.select, depth, mode);
                break;
            case LP_STMT_INSERT:
                node->u.insert.source = walk(node->u.insert.source, depth, mode);
                node->u.insert.upsert = walk(node->u.insert.upsert, depth, mode);
                walk_list(&node->u.insert.returning, depth, mode);
                break;
            case LP_STMT_UPDATE:
                walk_list(&node->u.update.set_clauses, depth, mode);
                node->u.update.from   = walk(node->u.update.from, depth, mode);
                node->u.update.where  = walk(node->u.update.where, depth, mode);
                walk_list(&node->u.update.order_by, depth, mode);
                node->u.update.limit  = walk(node->u.update.limit, depth, mode);
                walk_list(&node->u.update.returning, depth, mode);
                break;
            case LP_STMT_DELETE:
                node->u.del.where = walk(node->u.del.where, depth, mode);
                walk_list(&node->u.del.order_by, depth, mode);
                node->u.del.limit = walk(node->u.del.limit, depth, mode);
                walk_list(&node->u.del.returning, depth, mode);
                break;
            case LP_WITH:
                walk_list(&node->u.with.ctes, depth, mode);
                break;
            case LP_CTE:
                node->u.cte.select = walk(node->u.cte.select, depth, mode);
                break;
            default:
                break;  // leaves and not-yet-handled cases
        }
    }

    void walk_list(LpNodeList *list, int depth, XmlMode mode) {
        if (!list) return;
        for (int i = 0; i < list->count; i++)
            list->items[i] = walk(list->items[i], depth, mode);
    }

    // XML elements propagate the active mode through nested attributes
    // and children so jsx(<ul><li/></ul>) emits xml_element_jsx for
    // both the outer ul and the inner li.
    void walk_xml_children(LpNode *node, int depth, XmlMode mode) {
        auto& x = node->u.sqldeep_xml;
        for (int i = 0; i < x.attrs.count; i++) {
            LpNode *a = x.attrs.items[i];
            if (!a) continue;
            a->u.sqldeep_xml_attr.value =
                walk(a->u.sqldeep_xml_attr.value, depth, mode);
        }
        for (int i = 0; i < x.children.count; i++)
            x.children.items[i] = walk(x.children.items[i], depth, mode);
    }

    // ── Rewrites ─────────────────────────────────────────────────

    // Detect the "aggregate field" form: a sqldeep field whose value
    // is a SELECT with no FROM clause, exactly one result column, and
    // no other clauses. In sqldeep, `{field: SELECT expr}` (no FROM)
    // means "aggregate `expr` over the current GROUP BY scope" and
    // becomes 'field', fn_group_array(expr) — or just 'field', expr
    // when singular (SELECT/1).
    //
    // The grammar wraps the bare SELECT in LP_EXPR_SUBQUERY; we unwrap
    // it back to the right expression here.
    LpNode *maybe_aggregate_field_value(LpNode *v) {
        if (!v || v->kind != LP_EXPR_SUBQUERY) return v;
        LpNode *sel = v->u.subquery.select;
        if (!sel || sel->kind != LP_STMT_SELECT) return v;
        const auto& s = sel->u.select;
        if (s.from || s.where || s.having || s.with) return v;
        if (s.group_by.count || s.order_by.count || s.window_defs.count) return v;
        if (s.result_columns.count != 1) return v;
        LpNode *rc = s.result_columns.items[0];
        LpNode *expr = (rc && rc->kind == LP_RESULT_COLUMN)
                          ? rc->u.result_column.expr : rc;
        if (!expr) return v;
        // LIMIT 1 (from SELECT/1) → just the bare expr.
        if (s.limit) return expr;
        return make_function(arena_, fn_group_array(), {expr});
    }

    // { a, key: expr, "k": v, (e): v, a.b } →
    //   fn_object('a', a, 'key', expr, 'k', v, e, v, 'b', a.b)
    // Literal `true` / `false` values are wrapped via sqldeep_json('...')
    // so they serialize as JSON bools rather than integers (sqlite/vanilla).
    // Aggregate field form `field: SELECT expr` collapses to
    // 'field', fn_group_array(expr).
    LpNode *rewrite_object(LpNode *node) {
        std::vector<LpNode*> args;
        const auto& fields = node->u.sqldeep_object.fields;
        for (int i = 0; i < fields.count; i++) {
            LpNode *f = fields.items[i];
            if (!f) continue;
            const auto& sf = f->u.sqldeep_field;
            switch (sf.key_form) {
                case 0: { // bare
                    args.push_back(make_string_lit(arena_, sf.key_text));
                    args.push_back(make_column_ref(arena_, sf.key_text));
                    break;
                }
                case 1:  // named id : expr
                case 2:  // "string" : expr
                case 5: { // qualified  a.b
                    LpNode *v = maybe_aggregate_field_value(sf.value);
                    args.push_back(make_string_lit(arena_, sf.key_text));
                    args.push_back(wrap_json_bool(v));
                    break;
                }
                case 3: {  // (expr) : val
                    LpNode *v = maybe_aggregate_field_value(sf.value);
                    args.push_back(sf.key_expr);
                    args.push_back(wrap_json_bool(v));
                    break;
                }
                case 4:  // recursive children — handled by RECURSE expansion
                default:
                    break;
            }
        }
        node->kind = LP_EXPR_FUNCTION;
        std::memset(&node->u, 0, sizeof(node->u));
        node->u.function.name = arena_strdup(arena_, fn_object());
        for (auto *a : args) list_append(arena_, &node->u.function.args, a);
        return node;
    }

    // [ e1, e2, ... ] → fn_array(e1, e2, ...) with bool wrapping.
    LpNode *rewrite_array(LpNode *node) {
        LpNodeList elements = node->u.sqldeep_array.elements;
        node->kind = LP_EXPR_FUNCTION;
        std::memset(&node->u, 0, sizeof(node->u));
        node->u.function.name = arena_strdup(arena_, fn_array());
        for (int i = 0; i < elements.count; i++)
            list_append(arena_, &node->u.function.args,
                        wrap_json_bool(elements.items[i]));
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

    // <tag attrs>body</tag> →
    //   xml_element('tag', xml_attrs(...), child1, child2, ...)
    // Self-closing <tag/> uses 'tag/' as the literal string so the
    // runtime function knows to emit the void-element form.
    // Mode (XML / Jsonml / Jsx) selects the function family.
    LpNode *rewrite_xml(LpNode *node, XmlMode mode) {
        const auto& x = node->u.sqldeep_xml;
        const char *fn_el, *fn_attrs;
        switch (mode) {
            case XmlMode::Jsx:
                fn_el = "xml_element_jsx";
                fn_attrs = "xml_attrs_jsx";
                break;
            case XmlMode::Jsonml:
                fn_el = "xml_element_jsonml";
                fn_attrs = "xml_attrs_jsonml";
                break;
            default:
                fn_el = "xml_element";
                fn_attrs = "xml_attrs";
                break;
        }

        std::vector<LpNode*> args;

        // tag: bare for non-self-closing, with trailing "/" marker
        // for self-closing so the runtime emits <tag/> form.
        std::string tag = x.tag ? x.tag : "";
        if (x.self_closing) tag += "/";
        args.push_back(make_string_lit(arena_, tag));

        // attrs: xml_attrs('name', value, ...). Boolean attributes
        // (no `=value`) become sqldeep_json('true') wrappers via the
        // BLOB protocol so the runtime distinguishes them from
        // attr="1" / attr="true" string values.
        if (x.attrs.count > 0) {
            std::vector<LpNode*> attr_args;
            for (int i = 0; i < x.attrs.count; i++) {
                LpNode *a = x.attrs.items[i];
                if (!a) continue;
                const auto& av = a->u.sqldeep_xml_attr;
                attr_args.push_back(make_string_lit(arena_, av.name));
                if (av.value) {
                    if (av.dynamic) {
                        attr_args.push_back(wrap_json_bool(av.value));
                    } else {
                        attr_args.push_back(av.value);
                    }
                } else {
                    // Boolean attribute: <input disabled/>
                    const char *fn = fn_json_blob();
                    if (fn) {
                        attr_args.push_back(make_function(
                            arena_, fn,
                            {make_string_lit(arena_, "true")}));
                    } else {
                        attr_args.push_back(make_string_lit(arena_, "true"));
                    }
                }
            }
            args.push_back(make_function(arena_, fn_attrs, attr_args));
        }

        // children: text → string literal; nested element → recurse
        // (already rewritten in post-order); anything else is an
        // interpolation expression, with bool wrap applied.
        for (int i = 0; i < x.children.count; i++) {
            LpNode *c = x.children.items[i];
            if (!c) continue;
            if (c->kind == LP_SQLDEEP_XML_TEXT) {
                args.push_back(make_string_lit(
                    arena_, c->u.sqldeep_xml_text.text
                              ? c->u.sqldeep_xml_text.text : ""));
            } else {
                args.push_back(wrap_json_bool(c));
            }
        }

        node->kind = LP_EXPR_FUNCTION;
        std::memset(&node->u, 0, sizeof(node->u));
        node->u.function.name = arena_strdup(arena_, fn_el);
        for (auto *a : args) list_append(arena_, &node->u.function.args, a);
        return node;
    }

    // SELECT-level handling: singular → LIMIT 1; deep projection wrapping;
    // FROM-first flag cleared (canonical unparser emits standard order).
    LpNode *rewrite_select(LpNode *node) {
        // The from_first flag was a parser convenience; the unparser
        // does NOT emit FROM-first in canonical output. Clear it so
        // the output reads as standard SQL.
        node->u.select.sqldeep_from_first = 0;

        bool was_singular = node->u.select.sqldeep_singular;

        // SELECT/1 → LIMIT 1 (unless a LIMIT is already specified).
        if (was_singular) {
            node->u.select.sqldeep_singular = 0;
            if (!node->u.select.limit) {
                node->u.select.limit = make_limit(arena_, 1);
            }
        }

        // SELECT/1 [single_elem] FROM t → SELECT single_elem FROM t LIMIT 1.
        // The single-element array literal already became fn_array(elem) in
        // the post-order walk; unwrap it back to bare `elem` when singular.
        if (was_singular
            && node->u.select.result_columns.count == 1) {
            LpNode *rc = node->u.select.result_columns.items[0];
            LpNode *proj = (rc && rc->kind == LP_RESULT_COLUMN)
                              ? rc->u.result_column.expr : rc;
            if (proj && proj->kind == LP_EXPR_FUNCTION
                && proj->u.function.name
                && std::strcmp(proj->u.function.name, fn_array()) == 0
                && proj->u.function.args.count == 1) {
                LpNode *elem = proj->u.function.args.items[0];
                if (rc && rc->kind == LP_RESULT_COLUMN)
                    rc->u.result_column.expr = elem;
                else
                    node->u.select.result_columns.items[0] = elem;
            }
        }

        // Deep-projection wrapping. The rules (mirroring sqldeep):
        //
        //   Array projection [...] — ALWAYS wraps in fn_group_array,
        //   regardless of nesting. Single-element arrays unwrap to
        //   the bare element first; multi-element wrap the whole
        //   fn_array(...) call.
        //
        //   Object projection {...} — wraps only when the SELECT is a
        //   value subquery in an "aggregating" context: parent is
        //   LP_EXPR_SUBQUERY whose parent is LP_SQLDEEP_FIELD or
        //   LP_EXPR_SQLDEEP_ARRAY or LP_EXPR_IN.
        //
        //   Singular (/1) — never wrap, no group_array. The singular
        //   array-element unwrap already happened above.
        //
        // Scalar-subquery contexts (function args, WHERE comparisons,
        // arithmetic, EXISTS, etc.) do NOT wrap.

        if (node->u.select.limit) return node;  // singular / explicit LIMIT
        if (node->u.select.result_columns.count != 1) return node;

        LpNode *rc = node->u.select.result_columns.items[0];
        LpNode *proj = (rc && rc->kind == LP_RESULT_COLUMN)
                          ? rc->u.result_column.expr : rc;
        if (!proj || proj->kind != LP_EXPR_FUNCTION) return node;
        const char *pname = proj->u.function.name;
        if (!pname) return node;

        bool is_obj = std::strcmp(pname, fn_object()) == 0;
        bool is_arr = std::strcmp(pname, fn_array()) == 0;
        if (!is_obj && !is_arr) return node;

        // Helper: set the projection back.
        auto replace_projection = [&](LpNode *new_expr) {
            if (rc && rc->kind == LP_RESULT_COLUMN)
                rc->u.result_column.expr = new_expr;
            else
                node->u.select.result_columns.items[0] = new_expr;
        };

        // XML projection in a value-subquery whose grandparent is an
        // XML element (i.e. <ul>{SELECT <li/> FROM t}</ul>) gets
        // wrapped in the corresponding {xml,jsx,jsonml}_agg helper so
        // rows aggregate into one XML fragment.
        bool is_xml = std::strcmp(pname, "xml_element") == 0
                    || std::strcmp(pname, "xml_element_jsx") == 0
                    || std::strcmp(pname, "xml_element_jsonml") == 0;
        if (is_xml) {
            LpNode *parent = node->parent;
            if (!parent || parent->kind != LP_EXPR_SUBQUERY) return node;
            LpNode *gp = parent->parent;
            if (!gp || gp->kind != LP_EXPR_SQLDEEP_XML) return node;
            const char *agg = "xml_agg";
            if (std::strcmp(pname, "xml_element_jsx") == 0)    agg = "jsx_agg";
            if (std::strcmp(pname, "xml_element_jsonml") == 0) agg = "jsonml_agg";
            replace_projection(make_function(arena_, agg, {proj}));
            return node;
        }

        if (is_obj) {
            // Object wrap requires aggregating-context grandparent.
            LpNode *parent = node->parent;
            if (!parent || parent->kind != LP_EXPR_SUBQUERY) return node;
            LpNode *gp = parent->parent;
            if (!gp) return node;
            if (gp->kind != LP_SQLDEEP_FIELD &&
                gp->kind != LP_EXPR_SQLDEEP_ARRAY &&
                gp->kind != LP_EXPR_IN) return node;
            replace_projection(make_function(arena_, fn_group_array(), {proj}));
            return node;
        }

        // Array projection: unwrap single-element; wrap in group_array.
        LpNode *inner = (proj->u.function.args.count == 1)
                          ? proj->u.function.args.items[0] : proj;
        replace_projection(make_function(arena_, fn_group_array(), {inner}));
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

// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Tape generator for the sqldeep parity harness: `tapegen --seed <hex>
// --ticks <n>` writes a TSV script (kind, sql) to stdout — a pure function of
// the seed (splitmix64, no library RNG), as the harness protocol requires.
//
// Tick 0 creates a fixed four-table schema whose foreign keys follow the
// `<table>_id` convention so join paths (->, <-) resolve without FK metadata.
// Every later tick is one of: an INSERT/UPDATE/DELETE (raw SQL, shared
// state), a sqldeep view or snapshot (transpiled + executed — persisted
// transpiler output that later queries read, which is what makes divergence
// compound with horizon), or a sqldeep query drawn from the v0.21.0 feature
// set (objects, arrays, nested subqueries, SELECT/1, ->/<- join paths,
// FROM-first, renamed fields, expressions, passthrough).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint64_t kSplitmixGamma = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t kSplitmixMulA  = 0xbf58476d1ce4e5b9ULL;
constexpr uint64_t kSplitmixMulB  = 0x94d049bb133111ebULL;
constexpr int      kMantissaBits  = 53;

struct Rng {
    uint64_t state;
    uint64_t next() {
        state += kSplitmixGamma;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * kSplitmixMulA;
        z = (z ^ (z >> 27)) * kSplitmixMulB;
        return z ^ (z >> 31);
    }
    double unit() { return double(next() >> (64 - kMantissaBits)) / double(uint64_t(1) << kMantissaBits); }
    int below(int n) { return n <= 0 ? 0 : int(unit() * n); }
    template <size_t N> const char* pick(const char* const (&xs)[N]) { return xs[below(int(N))]; }
};

// Mix of tick kinds after the schema tick (cumulative probabilities).
constexpr double kPInsert   = 0.40;
constexpr double kPMutate   = 0.48;
constexpr double kPMaterial = 0.58;
constexpr double kPViewQuery = 0.15;   // share of queries that read a view

const char* const kSchema =
    "CREATE TABLE customers(customers_id INTEGER PRIMARY KEY, name TEXT, region TEXT);"
    "CREATE TABLE vendors(vendors_id INTEGER PRIMARY KEY, name TEXT, rating REAL);"
    "CREATE TABLE orders(orders_id INTEGER PRIMARY KEY, customers_id INTEGER, vendors_id INTEGER, total REAL);"
    "CREATE TABLE items(items_id INTEGER PRIMARY KEY, orders_id INTEGER, sku TEXT, qty INTEGER);";

const char* const kNames[]   = {"alice", "bob", "carol", "dave", "erin", "frank"};
const char* const kRegions[] = {"north", "south", "east", "west"};
const char* const kSkus[]    = {"bolt", "nut", "washer", "gear", "spring"};

struct State {
    int customers = 0, vendors = 0, orders = 0, items = 0, views = 0;
    std::vector<std::string> viewNames;
};

std::string insert(Rng& r, State& s) {
    char buf[256];
    switch (r.below(4)) {
    case 0:
        std::snprintf(buf, sizeof buf, "INSERT INTO customers VALUES(%d, '%s%d', '%s')",
                      ++s.customers, r.pick(kNames), s.customers, r.pick(kRegions));
        break;
    case 1:
        std::snprintf(buf, sizeof buf, "INSERT INTO vendors VALUES(%d, '%s-co', %d.5)",
                      ++s.vendors, r.pick(kNames), r.below(5));
        break;
    case 2:
        std::snprintf(buf, sizeof buf, "INSERT INTO orders VALUES(%d, %d, %d, %d)",
                      ++s.orders, 1 + r.below(s.customers + 1), 1 + r.below(s.vendors + 1),
                      r.below(1000));
        break;
    default:
        std::snprintf(buf, sizeof buf, "INSERT INTO items VALUES(%d, %d, '%s', %d)",
                      ++s.items, 1 + r.below(s.orders + 1), r.pick(kSkus), 1 + r.below(9));
        break;
    }
    return buf;
}

std::string mutate(Rng& r, const State& s) {
    char buf[256];
    switch (r.below(3)) {
    case 0:
        std::snprintf(buf, sizeof buf, "UPDATE orders SET total = total + %d WHERE orders_id %% %d = 0",
                      r.below(50), 2 + r.below(3));
        break;
    case 1:
        std::snprintf(buf, sizeof buf, "DELETE FROM items WHERE items_id = %d", 1 + r.below(s.items + 1));
        break;
    default:
        std::snprintf(buf, sizeof buf, "UPDATE customers SET region = '%s' WHERE customers_id = %d",
                      r.pick(kRegions), 1 + r.below(s.customers + 1));
        break;
    }
    return buf;
}

// A sqldeep query from the feature set both transpilers support.
std::string query(Rng& r, const State& s) {
    char buf[1024];
    switch (r.below(10)) {
    case 0:
        std::snprintf(buf, sizeof buf,
            "SELECT { customers_id, name, region } FROM customers ORDER BY customers_id");
        break;
    case 1:
        std::snprintf(buf, sizeof buf,
            "SELECT { customers_id, name, orders: SELECT { orders_id, total } FROM c->orders o "
            "ORDER BY orders_id } FROM customers c ORDER BY customers_id");
        break;
    case 2:
        std::snprintf(buf, sizeof buf,
            "FROM orders o WHERE total > %d ORDER BY orders_id SELECT { orders_id, total, "
            "vendor: SELECT/1 { name, rating } FROM o<-vendors v }", r.below(500));
        break;
    case 3:
        std::snprintf(buf, sizeof buf,
            "SELECT { customers_id, items: SELECT { sku, qty } FROM c->orders o->items i "
            "ORDER BY items_id } FROM customers c ORDER BY customers_id");
        break;
    case 4:
        std::snprintf(buf, sizeof buf,
            "SELECT { orders_id, pair: [total, customers_id], customer: SELECT/1 { name } "
            "FROM o<-customers c } FROM orders o ORDER BY orders_id");
        break;
    case 5:
        std::snprintf(buf, sizeof buf,
            "SELECT { id: customers_id, upper_name: upper(name), n: (SELECT count(*) FROM orders o "
            "WHERE o.customers_id = c.customers_id) } FROM customers c WHERE region = '%s' "
            "ORDER BY customers_id", r.pick(kRegions));
        break;
    case 6:
        std::snprintf(buf, sizeof buf,
            "FROM items WHERE qty >= %d ORDER BY items_id SELECT items_id, sku, qty", 1 + r.below(5));
        break;
    case 7:
        std::snprintf(buf, sizeof buf,
            "SELECT { customers_id, orders: SELECT { orders_id, items: SELECT { sku, qty } "
            "FROM o->items i ORDER BY items_id } FROM c->orders o ORDER BY orders_id } "
            "FROM customers c ORDER BY customers_id");
        break;
    case 8:
        std::snprintf(buf, sizeof buf,
            "SELECT/1 { customers_id, name } FROM customers WHERE customers_id = %d",
            1 + r.below(s.customers + 1));
        break;
    default:
        std::snprintf(buf, sizeof buf,
            "SELECT { customers_id, totals: SELECT [ total ] FROM c->orders o ORDER BY orders_id } "
            "FROM customers c ORDER BY customers_id");
        break;
    }
    return buf;
}

// gNoCtas drops CREATE TABLE ... AS <sqldeep> from the tape (--no-ctas). It
// exists so the narrow in-contract subset can be run separately from the full
// tape; both are gated. CTAS was the divergence the wide tape found (now
// fixed and goldened — see oracle/README.md), and the split is kept as a
// bisecting aid, not as a way to hide it.
bool gNoCtas = false;

// Persist transpiler output as a view (or a snapshot table) so later ticks
// read it: the port's mistakes accumulate in state, not just in one answer.
std::string materialise(Rng& r, State& s) {
    const bool table = r.below(2) == 0 && !gNoCtas;
    const std::string name = (table ? "snap" : "v") + std::to_string(++s.views);
    s.viewNames.push_back(name);
    const std::string q = query(r, s);
    return (table ? "CREATE TABLE " : "CREATE VIEW ") + name + " AS " + q;
}

std::string escapeTsv(const std::string& in) {
    std::string out;
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            default:   out += c; break;
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    uint64_t seed = 0;
    int ticks = 90;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-ctas")) { gNoCtas = true; continue; }
        if (i + 1 >= argc) break;
        if (!std::strcmp(argv[i], "--seed")) { seed = std::strtoull(argv[++i], nullptr, 16); }
        else if (!std::strcmp(argv[i], "--ticks")) { ticks = std::atoi(argv[++i]); }
    }
    Rng r{seed};
    State s;
    std::printf("@fields\tkind\tsql\n");
    for (int t = 0; t < ticks; ++t) {
        std::string kind, sql;
        if (t == 0) {
            kind = "exec"; sql = kSchema;
        } else {
            const double p = r.unit();
            if (p < kPInsert)        { kind = "exec";  sql = insert(r, s); }
            else if (p < kPMutate)   { kind = "exec";  sql = mutate(r, s); }
            else if (p < kPMaterial) { kind = "xexec"; sql = materialise(r, s); }
            else if (!s.viewNames.empty() && r.unit() < kPViewQuery) {
                kind = "query";
                sql = "SELECT * FROM " + s.viewNames[r.below(int(s.viewNames.size()))];
            } else { kind = "query"; sql = query(r, s); }
        }
        std::printf("%s\t%s\n", kind.c_str(), escapeTsv(sql).c_str());
    }
    return 0;
}

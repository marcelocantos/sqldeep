# Declarative Tree Construction from Flat Relations via Fixed-Point Comprehensions

## Abstract

SQL databases represent hierarchical data as flat tables with self-referential
foreign keys. Producing nested tree-shaped output (e.g., JSON) requires either
recursive SQL functions — unavailable in some engines — or client-side
assembly. We observe that this transformation is an anamorphism whose coalgebra
is fully determined by the schema, and therefore expressible as a structural
annotation rather than explicit recursion. We introduce a fixed-point marker
(`*`) within a JSON-comprehension syntax that declares where recursive nesting
occurs, and describe a compilation strategy — bracket injection — that produces
correct nested JSON entirely within SQL, including on engines without recursive
function support (e.g., SQLite). The result is the first comprehension-style
syntax for declarative table-to-tree transformation, implemented as a pure
string-to-string rewriter with no runtime dependency.

## 1. Introduction

### 1.1 The problem

A table `categories(id, name, parent_id)` encodes a tree. The tree is implicit
in the `parent_id` self-reference. Extracting it as nested JSON — where each
node contains its children, recursively — is a common need and a surprisingly
awkward one.

SQL's `WITH RECURSIVE` computes transitive closures but produces flat row sets.
PostgreSQL offers recursive SQL functions that can build nested `jsonb`, but
this is engine-specific and requires the user to write an explicit recursive
function. SQLite, MySQL, and most ORMs provide no path from recursive traversal
to nested output in a single query.

The awkwardness is disproportionate to the conceptual simplicity. A programmer
can state the requirement in one sentence — "give me each category with its
children, recursively" — but expressing it in SQL requires dozens of lines of
engine-specific code. The gap between intent and expression suggests a missing
abstraction.

### 1.2 Survey of existing approaches

We surveyed recursive data construction across 15 language families:

| System | Recursion style | Nested output? |
|--------|----------------|----------------|
| SQL recursive CTE | Explicit | No (flat rows) |
| SQL recursive function (PG) | Explicit | Yes |
| GraphQL | Fixed-depth only | Yes (by design) |
| LINQ / C# | Explicit (recursive method) | Yes |
| Haskell | Explicit (`unfoldForest`) | Yes |
| XSLT | Implicit (`apply-templates`) | Yes |
| XQuery | Explicit (recursive function) | Yes |
| jq | Explicit (recursive `def`) | Yes |
| Datalog | Implicit (fixpoint) | No (flat) |
| Prolog | Explicit (`findall` + recursion) | Yes (terms are trees) |
| SPARQL | Implicit (property paths) | No (flat) |
| Cypher | Implicit (variable-length `*`) | No (flat/paths) |
| K/Q | Explicit (`.z.s` self-reference) | Yes |
| ORMs (Prisma, EF, etc.) | Fixed-depth `include` | Partial |
| Pre-computed (ltree, closure, nested sets) | Eliminated | No (flat) |

**Finding**: No mainstream language offers a comprehension-style syntax that
transforms flat relational data into nested tree output without explicit
recursion. XSLT's implicit recursion via template matching is the closest
precedent, but it operates on already-tree-shaped input (XML), not flat
relations.

### 1.3 Why the gap exists

The finding is surprising given the maturity of each component technology.
Recursive CTEs have been part of the SQL standard since 1999. JSON aggregate
functions are now widespread across engines. Anamorphisms were formalised by
Meijer, Fokkinga, and Paterson in 1991. List comprehensions date to the 1970s.
Every ingredient has been available for years.

We attribute the gap to a disciplinary boundary effect. The three communities
whose knowledge intersects at this problem have each solved the adjacent
subproblem and stopped:

- **Database practitioners** built `WITH RECURSIVE` and considered recursion
  solved. Their output model is the flat row set; nested output is the
  application's concern.
- **Functional programming and category theory** formalised `unfoldTree` and
  anamorphisms as standard vocabulary. But these communities do not think in
  SQL, and their output medium is in-memory data structures, not serialised
  JSON from a query engine.
- **Query language designers** extended comprehension syntax to nested output
  via JSON constructors (`json_object`, `json_array`). But this line of work
  addressed fixed-depth nesting — one level of subquery per level of output —
  and was not extended to recursive structures.

Each community reached a natural stopping point within its own frame. The
result was not that the problem was considered hard, but that it was not
perceived as a single problem at all. The database community saw a recursion
problem (solved by CTEs). The PL community saw a type-theoretic problem (solved
by anamorphisms). The query language community saw a nesting problem (solved by
JSON comprehensions). The synthesis — a comprehension that is also an
anamorphism, compiled to SQL — required occupying all three positions
simultaneously.

### 1.4 Contribution

1. The observation that the table-to-tree transformation is an anamorphism
   whose coalgebra is fully determined by the schema, making explicit recursion
   unnecessary (§2).
2. A fixed-point annotation (`*`) within JSON-comprehension syntax that
   declares the recursive nesting point — adding no information beyond what the
   schema already contains (§3).
3. A bracket-injection compilation strategy that produces correct nested JSON
   entirely within SQL, including on engines lacking recursive functions (§4).
4. An analysis of the generality boundary: which recursive constructs admit
   comprehension-style rewriting, and where the theoretical limits lie (§2.4).

### 1.5 Note on presentation

This paper is structured to follow the order of discovery. Each section
reveals a property that was already present in the problem — the
schema-determined coalgebra (§2), the annotation that adds no information (§3),
the sort-key invariant that makes bracket injection correct (§4). The
contribution is not the invention of a mechanism but the recognition that these
properties compose into something that, despite the maturity of its components,
no existing system provides.

## 2. Theoretical framework

### 2.1 The tree as a fixed point

Let R be a relation with a self-referential foreign key (e.g.,
`categories(id, name, parent_id)` where `parent_id` references `id`). Define a
functor F over a set of field types:

    F(X) = { f₁: T₁, f₂: T₂, ..., children: List(X) }

The tree type is the least fixed point: `Tree = μF`, satisfying
`Tree ≅ F(Tree)`. Unfolding this isomorphism: a tree node contains scalar
fields f₁...fₙ and a list of subtrees, each of which has the same structure.

The *value* (a specific tree built from data) is the result of an
**anamorphism** (unfold). An anamorphism is parameterised by a **coalgebra** —
a function that maps a seed to one level of the functor:

    coalg : Seed → F(Seed)

Applied recursively, the anamorphism unfolds the seed into the full fixed
point. For our case, the seed is a parent ID, and the coalgebra queries the
relation:

    coalg(pid) = { row.f₁, row.f₂, ..., children: [row.id | row ← R, row.parent_id = pid] }

The children list contains new seeds (child IDs), which the anamorphism
unfolds in turn. The complete tree is:

    ana(coalg)(root_pid)

Or equivalently, with a fixpoint combinator:

    tree = fix (λself. λpid. [{ ...row, children: self(row.id) }
                               | row ← R, row.parent_id = pid])

### 2.2 The annotation adds no information

The critical observation: the coalgebra above is *fully determined by the
data*. Given:

- a relation R,
- a self-referential key pair (parent_id → id),
- a field projection (which columns to include), and
- a root condition (which rows are roots),

there is exactly one tree. No choices, no branching logic, no user-supplied
recursion body. The recursion structure is an intrinsic property of the schema.

This means the `*` marker in the syntax (§3) does not *specify* recursion — it
*reveals* it. The schema already contains the information that `parent_id`
references `id` in the same table. The field list already defines the functor.
The root condition already identifies the seeds. The `*` marker simply points
at the position where the fixed point unfolds — a position that is already
determined by the other components.

When recursion is data-determined rather than logic-determined, it can be
expressed as a **structural annotation** rather than a computational form.
The user declares the shape; the system computes the unique fixed point that
the shape and the data jointly determine.

### 2.3 The tree as a fixed point of self-join

An alternative framing illuminates why comprehension syntax is the natural
home for this construct. Consider iterated self-joins:

- R ⋈ R (on parent_id = id) gives parent-child pairs
- R ⋈ R ⋈ R gives grandparent-grandchild triples
- R ⋈ⁿ R gives n-generation chains

The tree is the limit:

    T = lim(n→∞) R ⋈ⁿ R

Each join level adds one generation. The limit is the complete tree with
nesting. The JSON shape is the projection at each level.

A non-recursive JSON comprehension (`SELECT { ... } FROM ... SELECT { ... }`)
already expresses one level of this join — a subquery nested inside a JSON
object. The fixed-point annotation extends this to the limit: instead of
writing n levels of nested `SELECT { }`, the user writes one level with `*`
marking where the nesting repeats. The comprehension syntax was already
expressing finite unfoldings; the `*` generalises it to the fixed point.

### 2.4 Generality boundary

Not all recursive constructs admit this treatment. We classify them by what
guides the recursion:

**Structural recursion** (data-guided):
- Trees (anamorphisms from acyclic self-referential data)
- DAGs (anamorphisms with duplication or ID-based references)
- Heterogeneous trees (mutual anamorphisms across multiple relations)
- Hylomorphisms (unfold then fold, with fusion opportunity)

All decidable, all terminate on finite acyclic data, all admit mechanical
compilation to SQL.

**Computed recursion** (logic-guided):
- Children determined by arbitrary predicates over data
- Turing-complete; undecidable termination
- Equivalent in power to unrestricted `WITH RECURSIVE`

The boundary between these corresponds to well-known distinctions in computer
science: Datalog vs. Prolog, total vs. general functional programming, regular
vs. recursively enumerable languages. In each pair, the first is decidable and
admits mechanical optimisation; the second is Turing-complete and does not.

For a query language, structural recursion is the productive design point. It
covers the vast majority of real-world recursive query needs — organisational
hierarchies, category trees, bill-of-materials, threaded comments, file
systems — while remaining decidable and optimisable.

A subtlety: the rewriter's simplicity is orthogonal to the construct's
expressive power. The rewriter performs a mechanical template substitution
(§4). The recursive computation happens in SQL, where `WITH RECURSIVE` is
Turing-complete. The rewriter does not limit what SQL can compute; it limits
what the *user* must write. This is the same division of labour as in any
compiler: the source language is restricted for the user's benefit; the target
language is general.

### 2.5 The output constraint

The real challenge is not recursive computation but **nested output from flat
SQL**. SQL already computes recursive transitive closures via `WITH RECURSIVE`.
The problem is that CTEs produce flat row sets. For non-recursive queries,
`json_object()` and `json_group_array()` bridge the gap between flat rows and
nested JSON. For recursive queries, these functions cannot be composed
recursively within the CTE framework.

Producing nested JSON from a recursive traversal requires one of:

1. **Recursive SQL functions** (PostgreSQL) — the function calls itself,
   building nested `jsonb` from the inside out. Engine-specific; unavailable in
   SQLite, MySQL, and most embedded engines.
2. **Client-side assembly** — the query returns flat rows annotated with depth
   or path; application code reconstructs the tree. Correct but defeats the
   purpose of a single-query solution.
3. **A novel in-SQL strategy** — the subject of Section 4.

## 3. Syntax

We extend an existing JSON-comprehension SQL transpiler with a fixed-point
annotation. The transpiler (sqldeep) rewrites a JSON-like DSL to standard SQL:

    SELECT { id, name } FROM t
    →  SELECT json_object('id', id, 'name', name) FROM t

Nested subqueries compose naturally:

    SELECT {
      id, name,
      orders: SELECT { total } FROM orders WHERE orders.cust_id = c.id
    } FROM customers c

The recursive extension adds a single new marker:

    SELECT/1 {
      id, name,
      children: *
    } FROM categories
      RECURSE ON (parent_id)
      WHERE parent_id IS NULL

Components:

- **`{ id, name, children: * }`** — the shape functor F. The scalar fields
  (`id`, `name`) define the per-node projection. `*` marks the fixed-point
  position: the slot where `List(F(Tree))` — the recursive children array —
  will be placed.
- **`RECURSE ON (parent_id)`** — declares the self-referential FK. The PK
  defaults to `id`; explicit form: `RECURSE ON (parent_id = category_id)`.
  This is the only schema annotation required — it names the edge that the
  anamorphism traverses.
- **`WHERE parent_id IS NULL`** — the root condition. Seeds the anamorphism:
  which rows have no parent and therefore form the top level of the tree.
- **`SELECT/1`** — singular select. Returns one root tree (as a JSON object).
  Without `/1`, returns a forest (JSON array of root trees).

The `*` marker is not a recursive function call. It is a **structural
declaration**: "the fixed point of this shape lives here." The system derives
the recursion mechanically from the schema annotation. As established in §2.2,
`*` adds no information that is not already present in the combination of the
field list, the `RECURSE ON` clause, and the root condition. It serves only to
make the fixed-point structure syntactically explicit — visible to the reader
and to the rewriter.

## 4. Compilation: bracket injection

### 4.1 The problem restated

Given the syntax in §3, the rewriter must produce a single SQL query that:

1. Traverses the self-referential relation recursively (straightforward via
   `WITH RECURSIVE`).
2. Produces nested JSON output where each node's `children` array contains its
   descendants to arbitrary depth (the hard part — §2.5).

### 4.2 Key insight

A depth-first traversal of a tree visits nodes in an order that fully
determines the bracket structure of the serialised output. Consider a tree:

```
A
├── B
│   └── D
└── C
```

DFS order: A, B, D, C. The nested JSON is:

```json
{"id":"A","children":[{"id":"B","children":[{"id":"D","children":[]}]},{"id":"C","children":[]}]}
```

The brackets open when we enter a node and close when we leave it. We do not
need recursive JSON functions to produce this. We need only the DFS order and a
way to emit opening and closing fragments at the correct positions.

This is not a trick. It is what tree serialisation *is*: the isomorphism
between a tree and its bracket word. The bracket-injection strategy does not
circumvent the lack of recursive JSON functions — it recognises that recursive
JSON functions were never necessary. DFS order plus bracket emission is the
definition of serialisation.

### 4.3 Enter and exit events

For each node in the tree, emit two rows:

- **Enter event**: the opening JSON fragment — all scalar fields and the
  opening of the children array: `{"id":1,"name":"A","children":[`
- **Exit event**: the closing bracket pair: `]}`

**Sort key**: the DFS path string for enter events; path + `char(127)` for exit
events. The path is constructed during the recursive CTE traversal by
concatenating zero-padded IDs with `/` separators (e.g., `0000000001/0000000003`).

The `char(127)` suffix guarantees correct nesting: since 127 is
lexicographically greater than any character used in paths (`/` = 47,
digits = 48–57), an exit event sorts *after* all of the node's descendant
enter and exit events, but *before* the next sibling's enter event. This is
not an arbitrary choice — it is the lexicographic encoding of "after all
descendants."

**Sibling commas**: `ROW_NUMBER() OVER (PARTITION BY parent_id ORDER BY pk)`.
The first child in each sibling group gets no comma prefix; subsequent siblings
prepend `,`. This localises the comma decision to each node, requiring no
lookahead or context.

**Final assembly**: `group_concat(fragment, '')` over events ordered by sort
key produces the complete nested JSON string.

### 4.4 Worked example

Consider a `categories` table with four rows:

| id | name | parent_id |
|----|------|-----------|
| 1 | Root | NULL |
| 2 | A | 1 |
| 3 | B | 1 |
| 4 | A1 | 2 |

**Stage 1: `_dfs`** (recursive CTE — depth-first traversal):

| id | name | parent_id | _depth | _path |
|----|------|-----------|--------|-------|
| 1 | Root | NULL | 0 | `0000000001` |
| 2 | A | 1 | 1 | `0000000001/0000000002` |
| 4 | A1 | 2 | 2 | `0000000001/0000000002/0000000004` |
| 3 | B | 1 | 1 | `0000000001/0000000003` |

**Stage 2: `_ranked`** (add JSON object and child rank):

| id | _path | _obj | _child_rank |
|----|-------|------|-------------|
| 1 | `0000000001` | `{"id":1,"name":"Root"}` | 1 |
| 2 | `…/0000000002` | `{"id":2,"name":"A"}` | 1 |
| 4 | `…/…/0000000004` | `{"id":4,"name":"A1"}` | 1 |
| 3 | `…/0000000003` | `{"id":3,"name":"B"}` | 2 |

Node 3 (B) has `_child_rank = 2` because it is the second child of Root.

**Stage 3: `_events`** (enter + exit rows, sorted by `_sort_key`):

| _sort_key | _fragment |
|-----------|-----------|
| `0000000001` | `{"id":1,"name":"Root","children":[` |
| `…/0000000002` | `{"id":2,"name":"A","children":[` |
| `…/…/0000000004` | `{"id":4,"name":"A1","children":[` |
| `…/…/0000000004⌂` | `]}` |
| `…/0000000002⌂` | `]}` |
| `…/0000000003` | `,{"id":3,"name":"B","children":[` |
| `…/0000000003⌂` | `]}` |
| `0000000001⌂` | `]}` |

(⌂ represents `char(127)`)

Note how the sort order produces correct nesting: A1's exit comes before A's
exit, which comes before B's enter (with comma prefix). B's exit comes before
Root's exit.

**Final output** (`group_concat` of fragments in order):

```json
{"id":1,"name":"Root","children":[{"id":2,"name":"A","children":[{"id":4,"name":"A1","children":[]}]},{"id":3,"name":"B","children":[]}]}
```

### 4.5 SQL template

```sql
WITH RECURSIVE
  _dfs(id, name, parent_id, _depth, _path) AS (
    SELECT id, name, parent_id, 0, printf('%010d', id)
    FROM categories WHERE parent_id IS NULL
    UNION ALL
    SELECT c.id, c.name, c.parent_id, d._depth + 1,
           d._path || '/' || printf('%010d', c.id)
    FROM categories c JOIN _dfs d ON c.parent_id = d.id
  ),
  _ranked AS (
    SELECT *,
           json_object('id', id, 'name', name) AS _obj,
           ROW_NUMBER() OVER (PARTITION BY parent_id ORDER BY id)
             AS _child_rank
    FROM _dfs
  ),
  _events(_sort_key, _fragment) AS (
    SELECT _path,
           CASE WHEN _child_rank > 1 THEN ',' ELSE '' END
           || substr(_obj, 1, length(_obj) - 1)
           || ',"children":['
    FROM _ranked
    UNION ALL
    SELECT _path || char(127), ']}'
    FROM _ranked
  )
SELECT group_concat(_fragment, '')
FROM (SELECT _fragment FROM _events ORDER BY _sort_key)
```

The rewriter fills in four template parameters: the table name, the FK/PK
column pair, the field projection (for `json_object`), and the root condition
(for the base case `WHERE` clause). The three-CTE bracket structure is
invariant across all inputs.

### 4.6 Correctness

The correctness of bracket injection rests on a single invariant:

> **Sort-key invariant**: For any node N with path P, all descendants of N
> have paths with prefix P, and N's exit event (key P⌂) sorts after all
> descendant events and before any non-descendant event.

**Proof sketch** (structural induction on tree depth):

*Base case*: A leaf node L has path P. Its enter event has key P; its exit
event has key P⌂. L has no descendants, so the invariant holds vacuously.

*Inductive step*: Assume the invariant holds for all subtrees of depth < k.
Consider a node N at depth k with path P and children C₁, C₂, ..., Cₘ
(ordered by PK). Child Cᵢ has path P/padded(Cᵢ.id). By the inductive
hypothesis, all descendants of Cᵢ have paths prefixed by P/padded(Cᵢ.id),
and Cᵢ's exit event sorts after all of Cᵢ's descendants.

Since path comparison is lexicographic:
- All events within Cᵢ's subtree (prefix P/padded(Cᵢ.id)) sort before
  all events within Cⱼ's subtree (prefix P/padded(Cⱼ.id)) when i < j,
  because padded(Cᵢ.id) < padded(Cⱼ.id) when IDs are ordered.
- N's exit event P⌂ sorts after all children's subtree events because
  `⌂` (char 127) > `/` (char 47) > any digit, so P⌂ > P/... for any
  continuation.
- N's exit event P⌂ sorts before any sibling's events because siblings
  have a different prefix at N's level.

Therefore the concatenation of all fragments in sort-key order produces
correctly nested JSON. ∎

The argument requires that the padded ID representation preserves PK ordering
under lexicographic comparison, which holds for non-negative integers with
sufficient zero-padding.

### 4.7 Complexity

The compilation produces O(n) work at each stage:

| Stage | Rows | Work per row |
|-------|------|-------------|
| `_dfs` (recursive CTE) | n | O(d) for path concatenation, where d = depth |
| `_ranked` (window function) | n | O(1) amortised |
| `_events` (enter + exit) | 2n | O(1) |
| Final sort + concatenation | 2n | O(n log n) for the sort |

Total: **O(n log n)** dominated by the event sort, or O(n · d) if path
construction dominates (d = max depth; for balanced trees d = O(log n)).

Space: O(n · d) for path strings. For typical hierarchies (thousands of nodes,
depth < 20), this is negligible.

### 4.8 Per-node field rendering

Scalar fields are rendered via `json_object(...)`, which handles type-correct
JSON formatting (strings quoted, numbers bare, nulls as `null`). The closing
brace is stripped via `substr(_obj, 1, length(_obj) - 1)`, and the children
array opening `,"children":[` is appended.

This design reuses the database engine's JSON serialisation rather than
reimplementing escaping and type formatting in string concatenation — a
pragmatic choice that delegates correctness to the engine's well-tested JSON
functions.

### 4.9 Backend polymorphism

The bracket-injection strategy is portable across SQL engines. The only
engine-specific components are function names and string primitives:

| Component | SQLite | PostgreSQL |
|-----------|--------|------------|
| Object builder | `json_object(...)` | `jsonb_build_object(...)` |
| Path padding | `printf('%010d', id)` | `lpad(id::text, 10, '0')` |
| High-byte sort | `char(127)` | `chr(127)` |
| Concatenation | `group_concat(f, '')` | `string_agg(f, '' ORDER BY k)` |

The rewriter selects the appropriate function names based on the target
backend. The three-CTE structure, the sort-key invariant, and the
enter/exit event model are identical across all backends.

PostgreSQL additionally supports recursive SQL functions, which enable a
simpler bottom-up compilation: a function that calls itself, aggregating
children via `jsonb_agg(jsonb_build_object(...))`. When available, this
strategy produces more readable SQL. The bracket-injection strategy serves as
the universal fallback for engines without recursive function support.

### 4.10 Limitations

**Integer primary keys**: The zero-padded path construction assumes
non-negative integer PKs. String or UUID PKs would require a different
encoding — e.g., fixed-width hex for UUIDs, or length-prefixed encoding for
variable-length strings. The sort-key invariant is preserved as long as the
encoding maintains PK ordering under lexicographic comparison.

**Acyclicity**: The recursive CTE will not terminate on cyclic data.
Production use requires either a schema-level acyclicity constraint, a depth
limit in the CTE (`WHERE _depth < max_depth`), or a visited-set check (which
SQLite's `WITH RECURSIVE` does not natively support). The rewriter could emit a
depth guard as a safety measure.

**JSON-only output**: The bracket-injection strategy produces a JSON string.
Other tree serialisation formats (XML, protocol buffers, nested arrays) would
require different bracket patterns but the same underlying DFS-order principle.

**Single self-referential table**: The current syntax handles one table with a
self-FK. Heterogeneous trees across multiple tables (§5.1) require an extended
syntax and a more complex CTE template.

## 5. Generalisations

### 5.1 Heterogeneous trees

Real-world hierarchies often span multiple tables: departments contain teams,
teams contain employees. This is not self-referential recursion but mutual
recursion across relations — a family of functors rather than a single one:

    F_dept(X, Y) = { name: String, teams: List(G_team(Y)) }
    G_team(Y)    = { name: String, members: List(Y) }

The existing non-recursive sqldeep syntax already handles this via nested
subqueries:

    SELECT {
      name,
      teams: SELECT {
        name,
        members: SELECT { name } FROM employees e WHERE e.team_id = t.id
      } FROM teams t WHERE t.dept_id = d.id
    } FROM departments d

An extended `*` syntax could express cases where the nesting pattern repeats:

    SELECT {
      name,
      subs: *
    } FROM org_units
      RECURSE ON (parent_unit_id)
      WHERE parent_unit_id IS NULL

The bracket-injection strategy extends naturally: each level uses its own
`json_object` template for field projection, while the DFS path, enter/exit
events, and sort-key invariant work identically. The CTE joins across tables
rather than self-joining, but the mechanical structure is the same.

### 5.2 Hylomorphisms (unfold then fold)

A common pattern: build a tree and then aggregate over it. For example, compute
each department's total budget as the sum of its own budget and all
subdepartments'. This is a **hylomorphism** — an unfold (anamorphism) followed
by a fold (catamorphism):

    hylo(alg, coalg) = alg ∘ fmap(hylo(alg, coalg)) ∘ coalg

A naive implementation materialises the intermediate tree (unfold to JSON, then
parse and aggregate). **Hylomorphism fusion** — a well-known optimisation in
functional programming — eliminates the intermediate structure entirely. The
fused computation traverses the data once, computing aggregates bottom-up
without materialising JSON.

In SQL terms, this means the bracket-injection CTE could be extended with
aggregate columns computed during the DFS pass. The recursive CTE already
visits every node; adding a bottom-up accumulation (via a second recursive pass
or a window function over the DFS result) avoids JSON construction entirely.

A syntax sketch:

    SELECT/1 {
      name,
      total_budget: SUM(budget) OVER RECURSE,
      children: *
    } FROM departments
      RECURSE ON (parent_id)
      WHERE parent_id IS NULL

Where `SUM(budget) OVER RECURSE` denotes a catamorphism over the recursively
constructed tree. This remains speculative but illustrates the algebraic
composability of the fixed-point comprehension approach.

### 5.3 DAGs and cycles

A self-referential relation may encode a DAG (multiple parents per node) or a
general graph (cycles). The anamorphism framework handles these with
well-known adaptations:

- **DAGs**: The unfold duplicates shared subtrees, producing a tree with
  repeated structure. This is semantically correct (each path through the DAG
  produces its own subtree in the output) but may produce exponentially large
  output. An alternative is to emit subtrees by reference (ID only) after the
  first occurrence, but this loses the self-contained tree property.
- **Cycles**: The unfold does not terminate. Cycle handling requires either a
  depth limit (truncation) or a visited set (which most SQL CTEs do not
  natively support, though PostgreSQL's `CYCLE` clause does). The output is a
  truncated tree, not a faithful representation of the graph.

In both cases, the output remains tree-shaped — this is a fundamental
constraint of JSON (and XML, and most serialisation formats). The
bracket-injection strategy handles DAGs and depth-limited cycles without
modification; only the CTE's termination condition changes.

## 6. Related work

- **Oracle CONNECT BY** (1979): The earliest SQL syntax for recursive
  traversal. Produces flat output with `LEVEL` and `SYS_CONNECT_BY_PATH`
  pseudo-columns. Hierarchical but not nested.

- **SQL:1999 recursive CTEs**: Standard recursive computation in SQL.
  Turing-complete. Produces flat row sets; no facility for nested output.

- **PostgreSQL recursive functions + `jsonb_agg`**: Engine-specific recursive
  SQL functions can build nested `jsonb` by calling themselves. The only
  mainstream SQL approach that produces nested output, but unavailable outside
  PostgreSQL.

- **XSLT `apply-templates`**: Implicit recursion via template matching.
  Produces nested output from nested input. The closest precedent for
  declarative recursive transformation, but operates on already-tree-shaped XML,
  not flat relations. The contribution of the present work is the flat-to-nested
  direction.

- **Haskell `unfoldTree` / `unfoldForest`**: Standard anamorphism combinators
  in `Data.Tree`. Require the user to supply the coalgebra as a function. Our
  observation is that when the coalgebra is schema-determined, even this is
  unnecessary.

- **GraphQL**: Tree-shaped queries that mirror the desired output structure.
  No arbitrary-depth recursion — nesting depth is fixed at query-writing time.
  Relay's `@connection` pattern handles pagination, not recursion.

- **Datalog / SPARQL**: Fixed-point computation over flat relations. Powerful
  recursive reasoning but no nested output. The fixed point is a set of tuples,
  not a tree.

- **Closure tables / nested sets / ltree** (Celko, 2004): Pre-computed tree
  encodings that materialise the transitive closure at write time. Query-time
  recursion is eliminated, but output remains flat. These are storage
  strategies, not query syntax.

## 7. Conclusion

The transformation from flat self-referential tables to nested tree output
is an anamorphism whose coalgebra is fully determined by the schema. The
recursion is not computed — it is *observed*. The `*` marker does not instruct
the system to recurse; it points at a structure that was already there.

This makes the transformation expressible as a structural annotation rather
than explicit recursion — a fixed-point comprehension. The bracket-injection
compilation strategy produces correct nested JSON on any SQL engine with
recursive CTEs and string concatenation, including those without recursive
function support. The rewriter remains a mechanical template substitution; SQL
handles the recursive computation.

The construct occupies a productive point in the design space: more expressive
than any existing query comprehension syntax for tree construction, yet
within the decidable and terminable fragment of recursive computation. It
generalises to heterogeneous trees, hylomorphisms, and DAG traversals while
the compilation strategy remains invariant.

Each component — the functor, the coalgebra, the anamorphism, the CTE, the
bracket serialisation — is individually well-understood. Recursive CTEs are
25 years old. Anamorphisms were formalised 35 years ago. JSON comprehensions
in SQL are a decade old. The pieces were all present. The contribution is
recognising that they compose — that the gap between "compute a recursive
traversal" and "produce nested output" is bridged not by a new computation
but by a different perspective on serialisation.

## References

- Meijer, E., Fokkinga, M., & Paterson, R. (1991). Functional programming
  with bananas, lenses, envelopes and barbed wire. *FPCA '91*.
- Celko, J. (2004). *Trees and Hierarchies in SQL for Smarties*. Morgan
  Kaufmann.
- ISO/IEC 9075-2:1999. SQL:1999 — recursive query processing.
- Gibbons, J. (2003). Origami programming. *The Fun of Programming*. Palgrave
  Macmillan. (Hylomorphism fusion.)

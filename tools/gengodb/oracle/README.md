# GPM / SPARQL result oracle

Simple Python oracle built around `rdflib`. This directory adds a result-correctness
check both for the GPM MLIR snippets under `test/gengodb/gpm/snippets/` and for the
real `.sparql` query files under `resources/sparql/`, run against `resources/ttl/`.

## Overview

- `gpm_queries.py` — a hand-written standard-SPARQL translation of each GPM MLIR snippet's query
- `generate_golden.py` — needs `pip install rdflib`. Runs each translated query through rdflib (an independent SPARQL engine) against `coffee.ttl` and writes the result as a JSON fixture under `golden/`. Only needs to be re-run by hand if `coffee.ttl` or a snippet's semantics change.
- `generate_sparql_golden.py` — needs `pip install rdflib`. Runs the *actual* `.sparql` query text under `resources/sparql/coffee/` and `resources/sparql/bsbm/` (unmodified, no hand-translation) through rdflib against `coffee.ttl` / `product.ttl` respectively, and writes fixtures under `golden/`. Coffee queries wrap their pattern in a `GRAPH <file://...#rdf>` block, so `coffee.ttl` is loaded into an rdflib `Dataset` under that exact graph IRI; bsbm queries have no `GRAPH` clause and are run against a plain `rdflib.Graph`. Skips `bsbm/9.sparql` (`DESCRIBE`) and `bsbm/12.sparql` (`CONSTRUCT`); those return triples, not a row-of-bindings table, and `run-sparql` doesn't implement either form yet anyway.
- `rdflib_golden.py` — shared helpers (`term_key`, `normalize_rows`, `write_golden`) used by both generator scripts to turn rdflib query results into the same typed-JSON fixture shape.
- `oracle_lib.py` / `check_result.py` — stdlib-only. Parse the pipe-table `run-mlir`/`run-sparql` prints on stdout into typed cells (IRI / blank node / literal, with numeric datatypes compared by value, not lexical form) and compare them against a `golden/*.json` fixture as **multisets of rows**, allowing a consistent relabeling of blank nodes (their labels are arbitrary; the fixture and LingoDB's blank node labels don't need to match, just play the same structural role).
- `mlir-oracle.sh` / `sparql-oracle.sh` — invoke `check_result.py` automatically for any snippet/query that has a matching `golden/<category>__<name>.json` file (name includes the `.mlir`/`.sparql` extension), in addition to their existing crash check. A snippet/query can now fail two ways: `FAIL` (non-zero exit / crash) or `MISMATCH` (ran fine, wrong rows). Each takes one directory (plus a matching ttl-dir) at a time.
- `all-oracle.sh` — the "run everything" entry point. Auto-discovers every leaf directory under `test/gengodb/` holding `.mlir` files and every dataset directory under `resources/sparql/`, runs both oracles over all of them, and prints an aggregate pass/fail/mismatch total (exit code = total failures across every directory).

## Regenerating the fixtures

```
pip install rdflib
python3 tools/gengodb/oracle/generate_golden.py         # GPM MLIR snippets
python3 tools/gengodb/oracle/generate_sparql_golden.py   # resources/sparql/{coffee,bsbm}
```

## Known mismatches (as of the rz/main verification pass, 2026-08-24)

One snippet currently reports `MISMATCH`, confirmed (reproduced many times) as a
real bug, not an oracle-translation issue:

- `filter/filter_iri_neq.mlir` — comparing an IRI constant with `!=` against an RDF term of a different kind (blank node, or a string-typed literal) silently excludes the row instead of evaluating to true.

`optional/optional_unbound_key_join.mlir` was initially flagged as a second
mismatch (a stale value supposedly leaking across two independent `OPTIONAL`
blocks reusing a variable), but that did not reproduce on repeat runs — the
snippet consistently matches the golden fixture. Treat that as a retracted
false positive from the verification session, not a real bug, unless it
recurs.

`sparql-oracle.sh resources/sparql/coffee resources/ttl/coffee` reports one `MISMATCH`:

- `coffee/triangles.sparql` — the query dedupes triangle permutations via
  `FILTER(... && ?a < ?b && ?a < ?c && ?b < ?c)`, an ordering comparison between IRIs.
  Per the SPARQL spec, `<` is only defined between same-typed numerics/strings/booleans/
  dateTimes — comparing IRIs with it is a type error, so a spec-strict engine (rdflib
  included) excludes every row and the golden fixture is `0` rows. LingoDB instead
  returns `1` row (the `ex:a`/`ex:b`/`ex:c` triangle), i.e. it evaluates `<` between IRIs
  as a lexicographic string comparison. This may be a deliberate, useful extension
  (it's exactly the standard idiom for deduping cyclic patterns) rather than a bug —
  worth a decision on whether to special-case the oracle for it (e.g. rewrite the
  reference query to `STR(?a) < STR(?b)`, which is spec-legal and gives the same
  answer) or to treat it as intentionally-undefined behavior that just needs
  documenting. Flagging rather than picking one, since it changes what "correct" means
  for every future IRI-ordering query, not just this one.

`sparql-oracle.sh resources/sparql/bsbm resources/ttl/bsbm` reports three `MISMATCH`
(plus three separate, unrelated `FAIL`s below), all one root cause:

- `bsbm/7.sparql`, `bsbm/10.sparql`, `bsbm/11.sparql` — all three project `bsbm:price`,
  a literal typed with the *custom* (non-XSD-builtin) datatype `bsbm:USD`
  (e.g. `"2703.12"^^bsbm:USD`). `run-sparql` prints these as an empty string (`""`,
  no value, no datatype) instead of the literal's lexical form + datatype IRI.
  Reproduced directly with a minimal query selecting one such triple, independent of
  `ORDER BY`/`OPTIONAL`/anything else in the three failing queries — looks like a real
  bug in how a custom (application-defined, non-XSD) datatype IRI is carried through
  `run-sparql`'s result printing, not an oracle-translation issue.

Three further queries `FAIL` (crash / non-zero exit, not `MISMATCH` — the frontend
rejects them before producing any rows to compare) on missing SPARQL features:
`bsbm/6.sparql` (`FILTER REGEX`), `bsbm/9.sparql` (`DESCRIBE`), `bsbm/12.sparql`
(`CONSTRUCT`). Golden fixtures exist for `6.sparql` (`0` rows — no product labels
match `/camera/i` in this dataset) for whenever `REGEX` support lands; `9`/`12` are
skipped by `generate_sparql_golden.py` since `DESCRIBE`/`CONSTRUCT` return triples, not
a bindings table.

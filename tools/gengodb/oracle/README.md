# GPM snippet result oracle

`run-snippets.sh` used to only check that a `.mlir` snippet ran without
crashing. That's not the same as checking the *answer* is right — a query
that silently returns the wrong rows still exits 0. This directory adds a
result-correctness check on top of that, for the snippets under
`test/gengodb/gpm/snippets/` that query `resources/ttl/coffee/coffee.ttl`.

## How it works

- `gpm_queries.py` — a hand-written standard-SPARQL translation of each GPM
  MLIR snippet's query (see the comments in that file for the handful of
  snippets whose GPM semantics needed a non-obvious reading).
- `generate_golden.py` — **offline only**, needs `pip install rdflib`. Runs
  each translated query through rdflib (an independent SPARQL engine) against
  `coffee.ttl` and writes the result as a JSON fixture under `golden/`. Only
  needs to be re-run by hand if `coffee.ttl` or a snippet's semantics change.
- `oracle_lib.py` / `check_result.py` — stdlib-only. Parse the pipe-table
  `run-mlir` prints on stdout into typed cells (IRI / blank node / literal,
  with numeric datatypes compared by value, not lexical form) and compare
  them against a `golden/*.json` fixture as **multisets of rows**, allowing a
  consistent relabeling of blank nodes (their labels are arbitrary — the
  fixture and lingodb's blank node labels don't need to match, just play the
  same structural role).
- `run-snippets.sh` invokes `check_result.py` automatically for any snippet
  that has a matching `golden/<category>__<name>.mlir.json` file, in addition
  to its existing crash check. A snippet can now fail two ways: `FAIL` (non-
  zero exit / crash) or `MISMATCH` (ran fine, wrong rows).

`optional/optional_chained_anchor.2.mlir` intentionally has no fixture: it
exercises a GPM-specific rule (a mandatory pattern reusing a variable an
earlier `OPTIONAL` may have left unbound requires that variable to actually
be bound) that plain SPARQL doesn't have, so a generic SPARQL oracle can't
validate it — see the comment in `gpm_queries.py`.

## Regenerating the fixtures

```
pip install rdflib
python3 tools/gengodb/oracle/generate_golden.py
```

## Known mismatches (as of the rz/main verification pass, 2026-08-24)

Two snippets currently report `MISMATCH` — both are confirmed real bugs, not
oracle-translation issues (see session notes / PR description for the full
root-cause writeup):

- `filter/filter_iri_neq.mlir` — comparing an IRI constant with `!=` against
  an RDF term of a different kind (blank node, or a string-typed literal)
  silently excludes the row instead of evaluating to true.
- `optional/optional_unbound_key_join.mlir` — reusing a variable bound by one
  `OPTIONAL` block inside a second, independent `OPTIONAL` block leaks a
  stale value from an unrelated row when the variable should be NULL.

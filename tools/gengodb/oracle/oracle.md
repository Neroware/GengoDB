# GPM snippet result oracle

Simple Python oracle built around `rdflib`. This directory adds a result-correctness check, for the snippets under `test/gengodb/gpm/snippets/` that query `resources/ttl/coffee/coffee.ttl`.

## Overview

- `gpm_queries.py` — a hand-written standard-SPARQL translation of each GPM MLIR snippet's query
- `generate_golden.py` — needs `pip install rdflib`. Runs each translated query through rdflib (an independent SPARQL engine) against `coffee.ttl` and writes the result as a JSON fixture under `golden/`. Only needs to be re-run by hand if `coffee.ttl` or a snippet's semantics change.
- `oracle_lib.py` / `check_result.py` — stdlib-only. Parse the pipe-table `run-mlir` prints on stdout into typed cells (IRI / blank node / literal, with numeric datatypes compared by value, not lexical form) and compare them against a `golden/*.json` fixture as **multisets of rows**, allowing a consistent relabeling of blank nodes (their labels are arbitrary; the fixture and LingoDB's blank node labels don't need to match, just play the same structural role).
- `run-snippets.sh` — invokes `check_result.py` automatically for any snippet that has a matching `golden/<category>__<name>.mlir.json` file, in addition to its existing crash check. A snippet can now fail two ways: `FAIL` (non-zero exit / crash) or `MISMATCH` (ran fine, wrong rows).

(!) `optional/optional_chained_anchor.2.mlir` intentionally has no fixture: it exercises a GPM-specific rule (a mandatory pattern reusing a variable an earlier `OPTIONAL` may have left unbound requires that variable to actually be bound) that plain SPARQL doesn't have, so a generic SPARQL oracle can't validate it (see the comment in `gpm_queries.py`).

## Regenerating the fixtures

```
pip install rdflib
python3 tools/gengodb/oracle/generate_golden.py
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

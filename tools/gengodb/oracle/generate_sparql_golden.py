#!/usr/bin/env python3
"""Offline tool: computes golden answer sets for the real SPARQL query files
under resources/sparql/{coffee,bsbm}/, using rdflib as an independent SPARQL
engine, and writes them as JSON fixtures under tools/gengodb/oracle/golden/
-- picked up automatically by tools/gengodb/sparql-oracle.sh's result check
(it looks for golden/<category>__<name>.json, where <category> is the
queries-dir basename, e.g. "coffee" or "bsbm").

Unlike generate_golden.py (which runs hand-translated, hand-derived queries
against coffee.ttl for the GPM MLIR snippets), this runs the *actual*
.sparql query text run-sparql executes, unmodified, straight through rdflib.

- coffee/*.sparql wrap their pattern in
  `GRAPH <file://resources/ttl/coffee/coffee.ttl#rdf> { ... }`, so
  coffee.ttl is loaded into an rdflib Dataset under that exact graph IRI
  and the query is run against the dataset unmodified.
- bsbm/*.sparql have no GRAPH clause and query the default graph, so
  product.ttl is loaded into a plain rdflib.Graph.

bsbm/9.sparql (DESCRIBE) and bsbm/12.sparql (CONSTRUCT) are skipped: they
return RDF triples, not the row-of-bindings table check_result.py compares,
and run-sparql rejects both forms outright ("not yet supported"), so there's
nothing to check either way yet.

Queries using the GengoDB-specific fuzzy `?t TRUTH OF { ... }` extension
(e.g. coffee/truth_crisp.sparql) are skipped too: rdflib can't parse that
non-standard construct, so there's no independent oracle for them.

This is NOT run as part of the build or test suite -- it requires `rdflib`
(`pip install rdflib`), and only needs to be re-run by hand if the ttl
fixtures or a query's text change.

Usage: python3 generate_sparql_golden.py
"""
import glob
import os
import re
import sys

try:
    import rdflib
except ImportError:
    sys.exit("generate_sparql_golden.py needs rdflib: pip install rdflib")

sys.path.insert(0, os.path.dirname(__file__))
import rdflib_golden as golden

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GOLDEN_DIR = os.path.join(os.path.dirname(__file__), "golden")

COFFEE_GRAPH_IRI = "file://resources/ttl/coffee/coffee.ttl#rdf"
SKIP = {"bsbm/9.sparql", "bsbm/12.sparql"}  # DESCRIBE / CONSTRUCT not yet supported
# list of exclusions, due to rdflib issues
SKIP_RDFLIB = {"coffee/agg_expr_arg_error.sparql", "coffee/agg_group_by_expr_noalias.sparql",
               "coffee/agg_computed_alias_chain.sparql"}
TRUTH_OF_RE = re.compile(r"\bTRUTH\s+OF\b", re.IGNORECASE)  # fuzzy extension, not standard SPARQL
UNORDERED_CONCAT_RE = re.compile(r'^#\s*oracle:\s*unordered-concat\s+\?(\w+)\s+"((?:[^"\\]|\\.)*)"\s*$', re.MULTILINE)


def load_coffee():
    ds = rdflib.Dataset()
    g = ds.graph(rdflib.URIRef(COFFEE_GRAPH_IRI))
    g.parse(os.path.join(REPO, "resources/ttl/coffee/coffee.ttl"), format="turtle")
    return ds


def load_bsbm():
    g = rdflib.Graph()
    g.parse(os.path.join(REPO, "resources/ttl/bsbm/product.ttl"), format="turtle")
    return g


def main():
    stores = {"coffee": load_coffee(), "bsbm": load_bsbm()}

    for category, store in stores.items():
        for path in sorted(glob.glob(os.path.join(REPO, "resources/sparql", category, "*.sparql"))):
            name = os.path.basename(path)
            rel = f"{category}/{name}"
            if rel in SKIP:
                print(f"skip {rel} (CONSTRUCT/DESCRIBE, not a row-table result)")
                continue
            if rel in SKIP_RDFLIB:
                print(f"skip {rel} (rdflib bug, no independent oracle)")
                continue
            with open(path) as f:
                query_text = f.read()
            if TRUTH_OF_RE.search(query_text):
                print(f"skip {rel} (TRUTH OF extension, not standard SPARQL)")
                continue
            result = store.query(query_text)
            rows = [tuple(golden.term_key(t) for t in row) for row in result]
            result_vars = [str(v) for v in result.vars]
            unordered = []
            for var, sep in UNORDERED_CONCAT_RE.findall(query_text):
                if var not in result_vars:
                    sys.exit(f"{rel}: unordered-concat directive names ?{var}, which is not projected")
                unordered.append([result_vars.index(var), sep.encode().decode("unicode_escape")])
            out_path = os.path.join(GOLDEN_DIR, f"{category}__{name}.json")
            n = golden.write_golden(out_path, rows, unordered or None)
            print(f"wrote {out_path} ({n} rows)")


if __name__ == "__main__":
    main()

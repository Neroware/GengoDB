#!/usr/bin/env python3
"""Offline tool: computes golden answer sets for the GPM MLIR snippets under
test/gengodb/gpm/snippets/, using rdflib as an independent SPARQL oracle
against resources/ttl/coffee/coffee.ttl, and writes them as JSON fixtures
under tools/gengodb/oracle/golden/.

This is NOT run as part of the build or test suite -- it requires `rdflib`
(`pip install rdflib`), and only needs to be re-run by hand if
resources/ttl/coffee/coffee.ttl or a snippet's query semantics change.
`check_result.py` (used by mlir-oracle.sh) only needs the stdlib and reads
the JSON this script produces.

Usage: python3 generate_golden.py
"""
import os
import sys

try:
    import rdflib
except ImportError:
    sys.exit("generate_golden.py needs rdflib: pip install rdflib")

sys.path.insert(0, os.path.dirname(__file__))
from gpm_queries import QUERIES
import rdflib_golden as golden

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TTL = os.path.join(REPO, "resources/ttl/coffee/coffee.ttl")
GOLDEN_DIR = os.path.join(os.path.dirname(__file__), "golden")


def main():
    g = rdflib.Graph()
    g.parse(TTL, format="turtle")
    os.makedirs(GOLDEN_DIR, exist_ok=True)

    for rel, query in QUERIES.items():
        rows = [tuple(golden.term_key(t) for t in row) for row in g.query(query)]
        out_path = os.path.join(GOLDEN_DIR, rel.replace("/", "__") + ".json")
        n = golden.write_golden(out_path, rows)
        print(f"wrote {out_path} ({n} rows)")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Offline tool: computes golden answer sets for the GPM MLIR snippets under
test/gengodb/gpm/snippets/, using rdflib as an independent SPARQL oracle
against resources/ttl/coffee/coffee.ttl, and writes them as JSON fixtures
under tools/gengodb/oracle/golden/.

This is NOT run as part of the build or test suite -- it requires `rdflib`
(`pip install rdflib`), and only needs to be re-run by hand if
resources/ttl/coffee/coffee.ttl or a snippet's query semantics change.
`check_result.py` (used by run-snippets.sh) only needs the stdlib and reads
the JSON this script produces.

Usage: python3 generate_golden.py
"""
import json
import os
import sys
from decimal import Decimal

try:
    import rdflib
except ImportError:
    sys.exit("generate_golden.py needs rdflib: pip install rdflib")

sys.path.insert(0, os.path.dirname(__file__))
from gpm_queries import QUERIES

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TTL = os.path.join(REPO, "resources/ttl/coffee/coffee.ttl")
GOLDEN_DIR = os.path.join(os.path.dirname(__file__), "golden")


def term_key(term):
    if isinstance(term, rdflib.URIRef):
        return ("uri", str(term))
    if isinstance(term, rdflib.BNode):
        return ("bnode", term)  # resolved to a small int per-fixture below
    if isinstance(term, rdflib.Literal):
        lang = term.language
        dt = term.datatype
        if lang:
            return ("litlang", str(term), lang.lower())
        local = str(dt).rsplit("#", 1)[-1] if dt else None
        NUMERIC_LOCAL = {
            "integer", "int", "long", "short", "byte", "nonNegativeInteger", "unsignedLong",
            "unsignedInt", "unsignedShort", "unsignedByte", "positiveInteger",
            "nonPositiveInteger", "negativeInteger", "decimal", "float", "double",
        }
        if local in NUMERIC_LOCAL:
            try:
                return ("num", str(Decimal(str(term))))
            except Exception:
                return ("lit", str(term), str(dt))
        if local == "boolean":
            return ("bool", str(term).strip().lower() in ("true", "1"))
        if dt is None or local == "string":
            return ("str", str(term))
        return ("lit", str(term), str(dt))
    if term is None:
        return ("null",)
    return ("raw", str(term))


def main():
    g = rdflib.Graph()
    g.parse(TTL, format="turtle")
    os.makedirs(GOLDEN_DIR, exist_ok=True)

    for rel, query in QUERIES.items():
        rows = [tuple(term_key(t) for t in row) for row in g.query(query)]
        # renumber blank nodes to small ints in a stable (sorted) order so
        # the fixture is deterministic across regenerations
        bnodes = []
        for row in rows:
            for c in row:
                if c[0] == "bnode" and c[1] not in bnodes:
                    bnodes.append(c[1])
        idx = {b: i for i, b in enumerate(bnodes)}
        norm_rows = [[("bnode", idx[c[1]]) if c[0] == "bnode" else list(c) for c in row] for row in rows]

        out_path = os.path.join(GOLDEN_DIR, rel.replace("/", "__") + ".json")
        with open(out_path, "w") as f:
            json.dump(norm_rows, f, indent=1)
        print(f"wrote {out_path} ({len(rows)} rows)")


if __name__ == "__main__":
    main()

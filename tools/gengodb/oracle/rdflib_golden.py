"""Shared helpers for computing golden SPARQL answer sets via rdflib.

Used by the offline generator scripts -- `generate_golden.py` (hand-translated
queries for the GPM MLIR snippets, run against resources/ttl/coffee/coffee.ttl)
and `generate_sparql_golden.py` (the real .sparql query files under
resources/sparql/, run through rdflib unmodified) -- both of which write
fixtures in the row-of-typed-cells JSON shape that check_result.py compares
LingoDB's actual output against.

Needs `rdflib` (`pip install rdflib`). Not imported by check_result.py, which
stays stdlib-only so the runtime *-oracle.sh result check has no extra
dependencies -- only the offline generators pay the rdflib cost.
"""
import json
import os
from decimal import Decimal

import rdflib

NUMERIC_LOCAL = {
    "integer", "int", "long", "short", "byte", "nonNegativeInteger", "unsignedLong",
    "unsignedInt", "unsignedShort", "unsignedByte", "positiveInteger",
    "nonPositiveInteger", "negativeInteger", "decimal", "float", "double",
}


def term_key(term):
    """Convert one rdflib term into the same JSON-friendly typed-tuple shape
    oracle_lib.parse_actual_cell produces from LingoDB's printed output, so
    the two sides can be compared directly."""
    if isinstance(term, rdflib.URIRef):
        return ("uri", str(term))
    if isinstance(term, rdflib.BNode):
        return ("bnode", term)  # resolved to a small int per-fixture by normalize_rows
    if isinstance(term, rdflib.Literal):
        lang = term.language
        dt = term.datatype
        if lang:
            return ("litlang", str(term), lang.lower())
        local = str(dt).rsplit("#", 1)[-1] if dt else None
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


def normalize_rows(rows):
    """rows: list of tuples of already-term_key'd cells. Renumbers blank
    nodes to small ints in a stable (first-appearance) order so the fixture
    is deterministic across regenerations."""
    bnodes = []
    for row in rows:
        for c in row:
            if c[0] == "bnode" and c[1] not in bnodes:
                bnodes.append(c[1])
    idx = {b: i for i, b in enumerate(bnodes)}
    return [[("bnode", idx[c[1]]) if c[0] == "bnode" else list(c) for c in row] for row in rows]


def write_golden(out_path, rows, unordered_concat=None):
    """rows: list of tuples of already-term_key'd cells. Writes the
    normalized fixture as JSON and returns the row count.

    unordered_concat: optional list of [column_index, separator] pairs for
    GROUP_CONCAT columns whose element order is engine-defined. If given, the
    fixture is written as {"rows": [...], "unordered_concat": [...]} instead
    of a bare row list (check_result.py accepts both)."""
    norm_rows = normalize_rows(rows)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    payload = {"rows": norm_rows, "unordered_concat": unordered_concat} if unordered_concat else norm_rows
    with open(out_path, "w") as f:
        json.dump(payload, f, indent=1)
    return len(rows)

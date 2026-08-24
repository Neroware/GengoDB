"""Shared library for comparing LingoDB GPM/SPARQL query output against a
precomputed "golden" answer set.

Only the standard library is used here so `check_result.py` (run as part of
`run-snippets.sh`) has no extra dependencies. `generate_golden.py` is the
separate, offline tool that actually computes golden answers (via rdflib) and
is not needed at test time.

Cell values are represented as small JSON-friendly tuples so they can be
compared in a type-aware way (e.g. "30" and "3.0E1" as the same xsd:int/float
value, or two different blank-node labels that play the same structural
role):

    ("uri", <iri string>)
    ("bnode", <label or int>)          -- identity resolved relative to the row set
    ("str", <lexical string>)          -- xsd:string / untyped literal
    ("litlang", <lexical>, <lang>)     -- language-tagged literal
    ("num", <decimal string>)          -- any numeric-family XSD datatype
    ("bool", <true|false>)
    ("lit", <lexical>, <datatype iri>) -- anything else (dates, etc.)
    ("null",)                          -- SPARQL unbound / OPTIONAL miss
"""
import re
import itertools
from collections import Counter
from decimal import Decimal, InvalidOperation

NUMERIC_LOCAL = {
    "integer", "int", "long", "short", "byte", "nonNegativeInteger", "unsignedLong",
    "unsignedInt", "unsignedShort", "unsignedByte", "positiveInteger",
    "nonPositiveInteger", "negativeInteger", "decimal", "float", "double",
}


def parse_table(text):
    """Parse the pipe-table `run-mlir`/`run-sparql` prints on stdout.
    Returns (header: list[str], rows: list[list[str]]) with raw cell text
    (still containing the outer quoting/typing lingodb prints)."""
    lines = text.splitlines()
    start = None
    for i, l in enumerate(lines):
        if l.startswith('|'):
            start = i
            break
    if start is None:
        return [], []
    header = [c.strip() for c in lines[start].strip().strip('|').split('|')]
    rows = []
    i = start + 2  # skip header + dashes
    while i < len(lines) and lines[i].startswith('|'):
        cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
        rows.append(cells)
        i += 1
    return header, rows


_iri_re = re.compile(r'^<(.*)>$')
_lit_re = re.compile(r'^"(.*)"(\^\^<(.*)>|@([A-Za-z-]+))?$', re.S)


def parse_actual_cell(raw):
    """Convert one raw printed cell (e.g. '"<http://x>"', '""30"^^<...int>"',
    'null') into a canonical tuple as described in the module docstring."""
    raw = raw.strip()
    if raw in ('null', ''):
        return ('null',)
    if not (raw.startswith('"') and raw.endswith('"')):
        return ('raw', raw)
    inner = raw[1:-1]
    m = _iri_re.match(inner)
    if m:
        return ('uri', m.group(1))
    if inner.startswith('_:'):
        return ('bnode', inner)
    m = _lit_re.match(inner)
    if m:
        value, _, dtype, lang = m.groups()
        if lang:
            return ('litlang', value, lang.lower())
        if dtype:
            local = dtype.rsplit('#', 1)[-1]
            if local in NUMERIC_LOCAL:
                try:
                    return ('num', str(Decimal(value)))
                except InvalidOperation:
                    return ('lit', value, dtype)
            if local == 'boolean':
                return ('bool', value.strip().lower() in ('true', '1'))
            return ('lit', value, dtype)
        return ('str', value)
    return ('str', inner)


def canonical_row(raw_cells):
    return tuple(parse_actual_cell(c) for c in raw_cells)


def _bnode_ids(rows):
    seen = []
    for row in rows:
        for c in row:
            if c[0] == 'bnode' and c[1] not in seen:
                seen.append(c[1])
    return seen


def rows_equal_bag(actual_rows, expected_rows, max_bnode_perm=8):
    """Compare two lists of canonical rows as multisets, allowing a
    consistent relabeling of blank nodes on the expected side (bounded
    brute-force permutation search -- fine for the handful of blank nodes
    that show up in these fixtures)."""
    a_bnodes = _bnode_ids(actual_rows)
    e_bnodes = _bnode_ids(expected_rows)
    if len(a_bnodes) != len(e_bnodes):
        return False, f"blank node count differs: actual={len(a_bnodes)} expected={len(e_bnodes)}"
    n = len(a_bnodes)
    if n > max_bnode_perm:
        return False, f"too many distinct blank nodes ({n}) to verify exactly"

    def substitute(rows, bnode_list, mapping):
        idx = {b: i for i, b in enumerate(bnode_list)}
        out = []
        for row in rows:
            out.append(tuple(('bnode', mapping[idx[c[1]]]) if c[0] == 'bnode' else c for c in row))
        return out

    actual_counter = Counter(substitute(actual_rows, a_bnodes, list(range(n))))
    for perm in itertools.permutations(range(n)):
        if Counter(substitute(expected_rows, e_bnodes, list(perm))) == actual_counter:
            return True, None
    return False, "no blank-node relabeling makes the row multisets equal"


def compare_rows(actual_raw_rows, expected_rows):
    """actual_raw_rows: list[list[str]] as returned by parse_table.
    expected_rows: list[list[tuple]] already-canonical rows (e.g. loaded
    from a golden fixture, with blank nodes pre-numbered as small ints)."""
    actual = [canonical_row(row) for row in actual_raw_rows]
    expected = [tuple(tuple(c) for c in row) for row in expected_rows]
    if len(actual) != len(expected):
        return False, f"row count differs: actual={len(actual)} expected={len(expected)}"
    return rows_equal_bag(actual, expected)

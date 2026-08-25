#!/usr/bin/env python3
"""Compares a run-mlir/run-sparql stdout capture against a golden fixture.

Usage: check_result.py <golden.json> < run-mlir-stdout
Exit code 0 + "MATCH ..." on stdout if the row multiset matches (allowing a
consistent blank-node relabeling); exit 1 + "MISMATCH ..." otherwise.
No dependency beyond the stdlib and oracle_lib.py.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from oracle_lib import parse_table, compare_rows


def main():
    if len(sys.argv) != 2:
        print("usage: check_result.py <golden.json> < run-mlir-stdout", file=sys.stderr)
        return 2
    with open(sys.argv[1]) as f:
        expected_rows = json.load(f)
    text = sys.stdin.read()
    header, rows = parse_table(text)
    if not header:
        print("MISMATCH no result table found in input")
        return 1
    ok, msg = compare_rows(rows, expected_rows)
    if ok:
        print(f"MATCH ({len(rows)} rows, verified against golden fixture)")
        return 0
    print(f"MISMATCH {msg}")
    return 1


if __name__ == "__main__":
    sys.exit(main())

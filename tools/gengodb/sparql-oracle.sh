#!/usr/bin/env bash
# Runs every .sparql query in a directory (and its group subdirectories,
# e.g. resources/sparql/coffee/{agg,bind,join,...}/) through run-sparql and
# reports pass/fail. Subdirectories named `errors` (expected-failure queries,
# e.g. resources/sparql/coffee-fuzzy/errors/) are skipped.
#
# Counterpart to mlir-oracle.sh: instead of exercising GPM-lowered MLIR
# snippets directly, this drives real SPARQL query text through the full
# SPARQL frontend (see resources/sparql/). In addition to checking that a
# query runs without crashing, queries that have a golden fixture under
# tools/gengodb/oracle/golden/ (see tools/gengodb/oracle/generate_golden.py)
# also get their *result* checked against that independently-computed (via
# rdflib) expected answer set (fixture name: the query's path relative to
# resources/sparql/ with `/` replaced by `__`, e.g. coffee/agg/agg_sample.sparql
# -> golden/coffee__agg__agg_sample.sparql.json), so a query that runs cleanly but returns the
# wrong rows is reported as a failure too, not just a silent pass.
#
# Usage:
#   tools/gengodb/sparql-oracle.sh [queries-dir] [ttl-dir]
#
# Defaults:
#   queries-dir = resources/sparql/coffee
#   ttl-dir     = resources/ttl/coffee
#
# Note: unlike run-mlir, run-sparql's "database" argument must point at the
# specific dataset directory the queries' GRAPH clauses resolve against
# (e.g. resources/ttl/coffee, not resources/ttl) -- so when pointing
# queries-dir at resources/sparql/bsbm, pass resources/ttl/bsbm as ttl-dir.
#
# Env overrides:
#   RUN_SPARQL path to the run-sparql binary (default: build/run-sparql)
#   TIMEOUT    per-query timeout in seconds (default: 30)
#   PYTHON3    interpreter used for the oracle result check (default: python3)
#
# Exit code is the number of failing/mismatching queries (0 = all passed).

set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." >/dev/null 2>&1 && pwd)"

QUERIES_DIR="${1:-$REPO_ROOT/resources/sparql/coffee}"
TTL_DIR="${2:-$REPO_ROOT/resources/ttl/coffee}"
RUN_SPARQL="${RUN_SPARQL:-$REPO_ROOT/build/run-sparql}"
TIMEOUT="${TIMEOUT:-30}"
PYTHON3="${PYTHON3:-python3}"
GOLDEN_DIR="$SCRIPT_DIR/oracle/golden"
CHECK_RESULT="$SCRIPT_DIR/oracle/check_result.py"

if [[ ! -x "$RUN_SPARQL" ]]; then
   echo "error: run-sparql binary not found or not executable at '$RUN_SPARQL'" >&2
   echo "       build it first (e.g. 'ninja run-sparql' in $REPO_ROOT/build) or set RUN_SPARQL=<path>" >&2
   exit 1
fi

if [[ ! -d "$QUERIES_DIR" ]]; then
   echo "error: queries directory not found: '$QUERIES_DIR'" >&2
   exit 1
fi

have_python=1
command -v "$PYTHON3" >/dev/null 2>&1 || have_python=0

QUERIES_DIR="$(cd -- "$QUERIES_DIR" && pwd)"
SPARQL_ROOT="$REPO_ROOT/resources/sparql"
mapfile -t queries < <(find "$QUERIES_DIR" -type d -name errors -prune -o -type f -name '*.sparql' -print | LC_ALL=C sort)

if [[ ${#queries[@]} -eq 0 ]]; then
   echo "error: no .sparql files found in '$QUERIES_DIR'" >&2
   exit 1
fi

if [[ "$QUERIES_DIR/" == "$SPARQL_ROOT/"* ]]; then
   key_root="$SPARQL_ROOT"
else
   key_root="$(dirname -- "$QUERIES_DIR")"
fi
failures=()
pass_count=0
checked_count=0

for query in "${queries[@]}"; do
   name="${query#"$QUERIES_DIR"/}"
   output="$(cd "$REPO_ROOT" && timeout "$TIMEOUT" "$RUN_SPARQL" "$query" "$TTL_DIR" 2>&1)"
   status=$?
   if [[ $status -ne 0 ]]; then
      echo "FAIL  $name (exit $status)"
      failures+=("$name")
      printf '%s\n' "$output" | sed 's/^/      | /'
      continue
   fi

   rel="${query#"$key_root"/}"
   golden="$GOLDEN_DIR/${rel//\//__}.json"
   if [[ $have_python -eq 1 && -f "$golden" ]]; then
      check_output="$(printf '%s\n' "$output" | "$PYTHON3" "$CHECK_RESULT" "$golden" 2>&1)"
      check_status=$?
      checked_count=$((checked_count + 1))
      if [[ $check_status -eq 0 ]]; then
         echo "PASS  $name  [$check_output]"
         pass_count=$((pass_count + 1))
      else
         echo "MISMATCH  $name  [$check_output]"
         failures+=("$name")
      fi
   else
      echo "PASS  $name"
      pass_count=$((pass_count + 1))
   fi
done

total=${#queries[@]}
echo
echo "$pass_count/$total queries passed ($checked_count checked against golden fixtures)"
if [[ ${#failures[@]} -gt 0 ]]; then
   echo "failed: ${failures[*]}"
fi

exit ${#failures[@]}

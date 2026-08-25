#!/usr/bin/env bash
# Runs mlir-oracle.sh and sparql-oracle.sh over every currently known MLIR
# snippet directory (under test/gengodb/) and SPARQL query directory (under
# resources/sparql/), and reports an aggregate pass/fail/mismatch summary.
#
# mlir-oracle.sh / sparql-oracle.sh each take one directory (plus a matching
# ttl-dir) at a time; this script discovers every leaf directory that
# actually holds .mlir / .sparql files and drives both oracles over all of
# them in one go -- the "run everything" entry point.
#
# Usage:
#   tools/gengodb/all-oracle.sh
#
# Env overrides: same as mlir-oracle.sh / sparql-oracle.sh (RUN_MLIR,
# RUN_SPARQL, TIMEOUT, PYTHON3) -- forwarded through to both.
#
# Exit code is the total number of failing/mismatching snippets and queries
# across every directory (0 = all passed).

set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." >/dev/null 2>&1 && pwd)"

MLIR_ORACLE="$SCRIPT_DIR/mlir-oracle.sh"
SPARQL_ORACLE="$SCRIPT_DIR/sparql-oracle.sh"

total_failures=0
run_count=0

# --- MLIR snippets: every leaf directory under test/gengodb/ that directly
# holds at least one .mlir file, all checked against resources/ttl/coffee
# (mlir-oracle.sh's default -- every snippet that actually loads a graph,
# GPM or gsubop, currently targets the coffee dataset; the rest ignore the
# ttl-dir argument entirely). ---
mapfile -t mlir_dirs < <(find "$REPO_ROOT/test/gengodb" -name '*.mlir' -printf '%h\n' | sort -u)

for dir in "${mlir_dirs[@]}"; do
   echo "=== mlir-oracle: ${dir#"$REPO_ROOT"/} (ttl-dir: resources/ttl/coffee) ==="
   "$MLIR_ORACLE" "$dir" "$REPO_ROOT/resources/ttl/coffee"
   status=$?
   total_failures=$((total_failures + status))
   run_count=$((run_count + 1))
   echo
done

# --- SPARQL queries: every dataset directory under resources/sparql/, each
# checked against its matching resources/ttl/<dataset> (run-sparql's
# "database" argument must be the specific dataset dir its queries' GRAPH
# clauses / default graph resolve against -- see sparql-oracle.sh). ---
shopt -s nullglob
sparql_dirs=("$REPO_ROOT"/resources/sparql/*/)
shopt -u nullglob

for dir in "${sparql_dirs[@]}"; do
   dir="${dir%/}"
   dataset="$(basename "$dir")"
   ttl_dir="$REPO_ROOT/resources/ttl/$dataset"
   echo "=== sparql-oracle: resources/sparql/$dataset (ttl-dir: resources/ttl/$dataset) ==="
   "$SPARQL_ORACLE" "$dir" "$ttl_dir"
   status=$?
   total_failures=$((total_failures + status))
   run_count=$((run_count + 1))
   echo
done

echo "======================================"
if [[ $run_count -eq 1 ]]; then
   echo "ran 1 oracle directory, $total_failures failing/mismatching total"
else
   echo "ran $run_count oracle directories, $total_failures failing/mismatching total"
fi

exit $total_failures

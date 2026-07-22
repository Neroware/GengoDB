#!/usr/bin/env bash
# Runs every .mlir snippet in a directory through run-mlir and reports pass/fail.
#
# Usage:
#   test/gengodb/gpm/run-snippets.sh [snippets-dir] [ttl-dir]
#
# Defaults:
#   snippets-dir = test/gengodb/gpm/snippets/bgp
#   ttl-dir      = resources/ttl
#
# Env overrides:
#   RUN_MLIR   path to the run-mlir binary (default: build/run-mlir)
#   TIMEOUT    per-snippet timeout in seconds (default: 30)
#
# Exit code is the number of failing snippets (0 = all passed).

set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." >/dev/null 2>&1 && pwd)"

SNIPPETS_DIR="${1:-$REPO_ROOT/test/gengodb/gpm/snippets/bgp}"
TTL_DIR="${2:-$REPO_ROOT/resources/ttl}"
RUN_MLIR="${RUN_MLIR:-$REPO_ROOT/build/run-mlir}"
TIMEOUT="${TIMEOUT:-30}"

if [[ ! -x "$RUN_MLIR" ]]; then
   echo "error: run-mlir binary not found or not executable at '$RUN_MLIR'" >&2
   echo "       build it first (e.g. 'ninja run-mlir' in $REPO_ROOT/build) or set RUN_MLIR=<path>" >&2
   exit 1
fi

if [[ ! -d "$SNIPPETS_DIR" ]]; then
   echo "error: snippets directory not found: '$SNIPPETS_DIR'" >&2
   exit 1
fi

shopt -s nullglob
snippets=("$SNIPPETS_DIR"/*.mlir)
shopt -u nullglob

if [[ ${#snippets[@]} -eq 0 ]]; then
   echo "error: no .mlir files found in '$SNIPPETS_DIR'" >&2
   exit 1
fi

failures=()
pass_count=0

for snippet in "${snippets[@]}"; do
   name="$(basename "$snippet")"
   output="$(cd "$REPO_ROOT" && timeout "$TIMEOUT" "$RUN_MLIR" "$snippet" "$TTL_DIR" 2>&1)"
   status=$?
   if [[ $status -eq 0 ]]; then
      echo "PASS  $name"
      pass_count=$((pass_count + 1))
   else
      echo "FAIL  $name (exit $status)"
      failures+=("$name")
      printf '%s\n' "$output" | sed 's/^/      | /'
   fi
done

total=${#snippets[@]}
echo
echo "$pass_count/$total snippets passed"
if [[ ${#failures[@]} -gt 0 ]]; then
   echo "failed: ${failures[*]}"
fi

exit ${#failures[@]}

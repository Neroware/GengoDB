#!/usr/bin/env bash
# Runs every .mlir snippet in a directory through run-mlir and reports pass/fail.
#
# In addition to checking that a snippet runs without crashing, snippets that
# have a golden fixture under tools/gengodb/oracle/golden/ (currently: the
# ones querying resources/ttl/coffee/coffee.ttl, see
# tools/gengodb/oracle/generate_golden.py) also get their *result* checked
# against that independently-computed (via rdflib) expected answer set, so a
# snippet that runs cleanly but returns the wrong rows is now reported as a
# failure too, not just a silent pass.
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
#   PYTHON3    interpreter used for the oracle result check (default: python3)
#
# Exit code is the number of failing/mismatching snippets (0 = all passed).

set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." >/dev/null 2>&1 && pwd)"

SNIPPETS_DIR="${1:-$REPO_ROOT/test/gengodb/gpm/snippets/bgp}"
TTL_DIR="${2:-$REPO_ROOT/resources/ttl}"
RUN_MLIR="${RUN_MLIR:-$REPO_ROOT/build/run-mlir}"
TIMEOUT="${TIMEOUT:-30}"
PYTHON3="${PYTHON3:-python3}"
GOLDEN_DIR="$SCRIPT_DIR/oracle/golden"
CHECK_RESULT="$SCRIPT_DIR/oracle/check_result.py"

if [[ ! -x "$RUN_MLIR" ]]; then
   echo "error: run-mlir binary not found or not executable at '$RUN_MLIR'" >&2
   echo "       build it first (e.g. 'ninja run-mlir' in $REPO_ROOT/build) or set RUN_MLIR=<path>" >&2
   exit 1
fi

if [[ ! -d "$SNIPPETS_DIR" ]]; then
   echo "error: snippets directory not found: '$SNIPPETS_DIR'" >&2
   exit 1
fi

have_python=1
command -v "$PYTHON3" >/dev/null 2>&1 || have_python=0

shopt -s nullglob
snippets=("$SNIPPETS_DIR"/*.mlir)
shopt -u nullglob

if [[ ${#snippets[@]} -eq 0 ]]; then
   echo "error: no .mlir files found in '$SNIPPETS_DIR'" >&2
   exit 1
fi

category="$(basename "$SNIPPETS_DIR")"
failures=()
pass_count=0
checked_count=0

for snippet in "${snippets[@]}"; do
   name="$(basename "$snippet")"
   output="$(cd "$REPO_ROOT" && timeout "$TIMEOUT" "$RUN_MLIR" "$snippet" "$TTL_DIR" 2>&1)"
   status=$?
   if [[ $status -ne 0 ]]; then
      echo "FAIL  $name (exit $status)"
      failures+=("$name")
      printf '%s\n' "$output" | sed 's/^/      | /'
      continue
   fi

   golden="$GOLDEN_DIR/${category}__${name}.json"
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

total=${#snippets[@]}
echo
echo "$pass_count/$total snippets passed ($checked_count checked against golden fixtures)"
if [[ ${#failures[@]} -gt 0 ]]; then
   echo "failed: ${failures[*]}"
fi

exit ${#failures[@]}

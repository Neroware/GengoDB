# LingoDB — CLAUDE.md

## What this project is

LingoDB is a research SQL query engine that compiles queries to native code via MLIR (LLVM's Multi-Level Intermediate Representation). The core idea is *declarative sub-operators* — an intermediate dialect that enables cross-domain compiler optimizations. SQL is progressively lowered through several MLIR dialects before reaching LLVM IR and JIT-compiled machine code.

## Build system

Uses **CMake + Ninja**. Build outputs go into `build/lingodb-<type>/` (via `make`) or a flat `build/` directory (direct cmake invocation). The pre-built binaries in `/workspace/build/` are from a direct cmake run.

```bash
# Debug build (recommended for development)
make build-debug

# Release build
make build-release

# ASAN build
make build-asan

# Direct cmake (single build dir, what's currently in build/)
cmake -G Ninja . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -- -j$(nproc)
```

Key CMake options:
- `ENABLE_PYTHON`: `OFF` (default) | `CPYTHON` | `WASM` — Python UDF support
- `ENABLE_BASELINE_BACKEND`: `OFF` (default) | `ON` — TPDE baseline backend
- `ENABLE_MIMALLOC`: `ON` (default) — fast allocator
- `ENABLE_REFCOUNT`: `ON` (default) — reference counting for managed runtime values
- `ENABLE_GPU_BACKEND`: `OFF` (default) — GPU backend (in progress)

## Key binaries (in `build/`)

| Binary | Purpose |
|---|---|
| `sql` | Interactive SQL REPL (with linenoise readline support) |
| `run-sql` | Run a SQL file non-interactively |
| `run-mlir` | Execute a `.mlir` file directly |
| `mlir-db-opt` | MLIR optimizer/pass runner (like `mlir-opt`) |
| `sql-to-mlir` | Dump the MLIR produced from a SQL query |
| `sqlite-tester` | Run SQLite-format `.test` files |
| `tester` | Catch2 unit test runner |
| `compile-to-cpp` | Compile SQL to C++ source |

## Testing

Three test suites, all gated behind building the relevant targets first:

```bash
# All tests (requires resources/data/test and resources/data/uni to be built first)
make run-test

# Individual suites
make test-no-rebuild-unit    # Catch2 unit tests (build/tester)
make test-no-rebuild-lit     # LLVM lit tests (test/lit/, .mlir/.sql/.py files)
make test-no-rebuild-sqlite-small  # SQLite compat tests (test/sqlite-small/)

# Run a single unit test binary directly
./build/tester [test-name-filter]

# Run lit tests manually
lit -v build/lingodb-debug/test/lit -j 1
```

Test data is generated via `make resources/data/test/.stamp` and `resources/data/uni/.stamp`, which invokes scripts in `tools/generate/` and then loads data using the `sql` binary.

## Code conventions

- **C++20**, namespace `lingodb::` with sub-namespaces per subsystem (`lingodb::catalog`, `lingodb::compiler`, `lingodb::runtime`, etc.)
- **3-space indentation**, `PointerAlignment: Left`, no column limit — enforced by `.clang-format` (clang-format-20)
- Header guards use `#ifndef LINGODB_<SUBSYSTEM>_<FILE>_H` style
- Include order (enforced by clang-format): project headers first (`"lingodb/..."`), then LLVM/MLIR, then system
- No license headers in source files
- Linting: `make lint` runs `clang-tidy-20` via `tools/scripts/run-clang-tidy.py`
- Formatting: `make format` runs `clang-format-20` over `include/`, `src/`, `tools/`, `test/`

## Architecture: compilation pipeline

```
SQL text
  → Frontend       (libpg_query parse + AST + type analysis)
  → RelAlg dialect (relational algebra: joins, filters, projections, aggregations)
  → SubOp dialect  (declarative sub-operators — the research contribution)
  → TupleStream    (imperative tuple-level control flow)
  → DB / Arrow / util dialects (type lowering, Arrow column ops)
  → LLVM IR
  → JIT native code
```

### Source layout

```
include/lingodb/          # Public headers
  catalog/                # Table/column/function catalog
  compiler/
    Dialect/              # MLIR dialect definitions (RelAlg, SubOperator, TupleStream, DB, Arrow, PyInterp, util)
    Conversion/           # Lowering passes between dialects
    frontend/             # SQL parser, AST, SQL→MLIR translator
  execution/              # Backend drivers (LLVM JIT, C emitter, baseline)
  runtime/                # Runtime support called from compiled queries
  scheduler/              # Parallel task scheduler
  utility/                # Serialization, HyperLogLog, etc.

src/                      # Implementations (mirrors include/ structure)
  compiler/
    Dialect/              # Dialect op implementations, TableGen-generated code
    Conversion/           # Pass implementations (RelAlgToSubOp, SubOpToControlFlow, DBToStd, …)
    frontend/             # SQL parsing, AST nodes, sql_mlir_translator
  execution/              # Backend*.cpp, Execution.cpp, Frontend.cpp

tools/                    # Developer/build tools
  standalone-query/       # compile-to-cpp tool
  ct/                     # Compliance tester
  pass-profiler/          # MLIR pass timing profiler
  python/                 # Python pip package (pybridge)
  scripts/                # run-clang-tidy.py and other helpers
  generate/               # Dataset generation scripts (TPC-H, etc.)

vendored/
  libpg_query/            # PostgreSQL SQL parser (C library)
  mimalloc/               # Fast allocator
  linenoise-ng/           # Readline replacement for the REPL
  tpde/                   # TPDE baseline backend
```

## MLIR dialect overview

| Dialect | Level | Description |
|---|---|---|
| `RelAlg` | High | Relational algebra ops: `relalg.join`, `relalg.filter`, `relalg.aggregation`, etc. |
| `SubOperator` | Mid | Declarative sub-operators; enables cross-plan-node optimization |
| `TupleStream` | Mid-Low | Imperative tuple iteration, introduces loops and conditionals |
| `DB` | Low | Database types: nullable values, decimals, dates, strings |
| `Arrow` | Low | Apache Arrow table/column access |
| `PyInterp` | Low | Python UDF embedding (lowered to CPython or WASM calls) |
| `util` | Low | Utility ops, lowered to LLVM |

## Runtime library

`include/lingodb/runtime/` contains C++ structs callable directly from JIT-compiled code:
- Hash structures: `Hashtable`, `LazyJoinHashtable`, `PreAggregationHashtable`, `HashMultiMap`
- Buffers/collections: `GrowingBuffer`, `Heap`, `SegmentTreeView`
- Sort: `Sorting`
- Arrow I/O: `ArrowTable`, `ArrowColumn`, `ArrowView`
- Type ops: `StringRuntime`, `DecimalRuntime`, `DateRuntime`, `IntegerRuntime`, `FloatRuntime`
- State: `ThreadLocal`, `SimpleState`, `ExecutionContext`, `Session`

## Docker

```bash
make build-docker-dev      # Dev image (tools/docker/Dockerfile)
make build-docker-py-dev   # Python package dev image
```

## Coverage

```bash
make coverage   # builds with clang-20, runs tests, generates HTML report in build/coverage-report/
```

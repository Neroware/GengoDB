// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// variant.create_scalar allocas fresh per dynamic execution (see the design
// notes in VariantToStd.cpp) rather than hoisting/reusing a single slot --
// this is a regression test that doing so inside a many-iteration loop does
// not overflow the stack, and that each iteration's variant is independently
// correct (not aliased with a previous iteration's).
module {
    func.func @main() {
        %false = arith.constant 0 : i1
        %lb = arith.constant 0 : index
        %ub = arith.constant 200000 : index
        %ub2 = arith.constant 30000 : index
        %step = arith.constant 1 : index
        %c0 = arith.constant 0 : i64

        %sum = scf.for %i = %lb to %ub step %step iter_args(%acc = %c0) -> (i64) {
            %iInt = arith.index_cast %i : index to i64
            %v = variant.create_scalar %iInt : i64
            %got = variant.variant_get_val %v -> i64
            %newAcc = arith.addi %acc, %got : i64
            scf.yield %newAcc : i64
        }
        %sumAsDb = db.as_nullable %sum : i64, %false -> !db.nullable<i64>
        //CHECK: int(19999900000)
        db.runtime_call "DumpValue" (%sumAsDb) : (!db.nullable<i64>) -> ()

        // Same stress, but routed through `to_string` each iteration -- this
        // passes the alloca's address to an opaque runtime call, which
        // defeats LLVM's mem2reg/SROA promotion and is the scenario the
        // alloca-placement design note is actually about. Accumulate via
        // `db.hash` (rather than dumping each iteration's string) just to
        // force the value to be used without printing 200000 lines.
        %hashAcc = scf.for %i = %lb to %ub2 step %step iter_args(%acc = %lb) -> (index) {
            %iInt = arith.index_cast %i : index to i64
            %v = variant.create_scalar %iInt : i64
            %s = variant.to_string %v -> !db.string
            %h = db.hash %s : !db.string
            %newAcc = arith.xori %acc, %h : index
            scf.yield %newAcc : index
        }
        %isZero = arith.cmpi eq, %hashAcc, %lb : index
        %isZeroAsDb = db.as_nullable %isZero : i1, %false -> !db.nullable<i1>
        //CHECK: bool(false)
        db.runtime_call "DumpValue" (%isZeroAsDb) : (!db.nullable<i1>) -> ()
        return
    }
}

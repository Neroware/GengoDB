// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// Same-tag comparisons take the native load+cmp fast path (no runtime call);
// cross-tag numeric comparisons (Long vs Double) fall back to a single
// runtime call that reuses rdf4cpp's Literal comparison/promotion.
module {
    func.func @main() {
        %i5 = arith.constant 5 : i64
        %i7 = arith.constant 7 : i64
        %f5 = arith.constant 5.0 : f64
        %f9 = arith.constant 9.0 : f64

        %v5 = variant.create_scalar %i5 : i64
        %v7 = variant.create_scalar %i7 : i64
        %vf5 = variant.create_scalar %f5 : f64
        %vf9 = variant.create_scalar %f9 : f64

        // same-tag fast path
        %eqSame = variant.cmp eq %v5, %v5 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%eqSame) : (!db.nullable<i1>) -> ()

        %ltSame = variant.cmp lt %v5, %v7 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%ltSame) : (!db.nullable<i1>) -> ()

        %gtSameF = variant.cmp gt %vf9, %vf5 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%gtSameF) : (!db.nullable<i1>) -> ()

        // cross-tag numeric fallback
        %eqCross = variant.cmp eq %v5, %vf5 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%eqCross) : (!db.nullable<i1>) -> ()

        %ltCross = variant.cmp lt %v5, %vf9 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%ltCross) : (!db.nullable<i1>) -> ()

        // Same-tag comparison for a numeric-family member outside the
        // Bool/Long/Double native fast lane (see CmpOpLowering's
        // `ifRemainingNumeric` branch) -- both operands here are
        // create_scalar'd scratch-alloca pointers, exactly the case that
        // would be undefined behavior if this fell through to
        // `compareLiteralRefRef` (which assumes graph residency) instead.
        %by5 = arith.constant 5 : i8
        %by9 = arith.constant 9 : i8
        %vBy5 = variant.create_scalar %by5 : i8
        %vBy9 = variant.create_scalar %by9 : i8
        %ltByte = variant.cmp lt %vBy5, %vBy9 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%ltByte) : (!db.nullable<i1>) -> ()

        // Same-tag string comparison: native `!util.varlen32` load +
        // StringRuntime, no runtime dispatch on the variant's tag itself.
        %sApple = db.constant ("apple") : !db.string
        %sBanana = db.constant ("banana") : !db.string
        %vApple = variant.create_scalar %sApple : !db.string
        %vBanana = variant.create_scalar %sBanana : !db.string

        %eqStr = variant.cmp eq %vApple, %vApple -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%eqStr) : (!db.nullable<i1>) -> ()

        %ltStr = variant.cmp lt %vApple, %vBanana -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%ltStr) : (!db.nullable<i1>) -> ()

        %neqStr = variant.cmp neq %vApple, %vBanana -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%neqStr) : (!db.nullable<i1>) -> ()

        // cross-tag string vs. numeric: incomparable -> null (SPARQL's third
        // truth value), same convention as any other mismatched-family pair.
        %crossStrNum = variant.cmp eq %vApple, %v5 -> !db.nullable<i1>
        //CHECK: bool(NULL)
        db.runtime_call "DumpValue" (%crossStrNum) : (!db.nullable<i1>) -> ()

        return
    }
}

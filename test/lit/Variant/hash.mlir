// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// db.hash on !variant.variant. The central correctness requirement: whenever
// variant.cmp (see Variant/cmp.mlir) would report two variants equal, db.hash
// must produce the same hash for both -- most importantly across the
// cross-type numeric promotion variant.cmp implements (e.g. Long(5) ==
// Double(5.0)), see LowerToStd.cpp's HashLowering::hashImpl variant::VariantType
// branch and VariantRuntime::hashNumeric.

module {
    func.func @main() {
        %i5a = arith.constant 5 : i64
        %i5b = arith.constant 5 : i64
        %i6 = arith.constant 6 : i64
        %f5 = arith.constant 5.0 : f64
        %bTrue = arith.constant 1 : i1
        %i1 = arith.constant 1 : i64
        %str1 = db.constant ("hello") : !db.string
        %str2 = db.constant ("hello") : !db.string
        %str3 = db.constant ("world") : !db.string

        %vLong5a = variant.create_scalar %i5a : i64
        %vLong5b = variant.create_scalar %i5b : i64
        %vLong6 = variant.create_scalar %i6 : i64
        %vDouble5 = variant.create_scalar %f5 : f64
        %vBoolTrue = variant.create_scalar %bTrue : i1
        %vLong1 = variant.create_scalar %i1 : i64
        %vStr1 = variant.create_scalar %str1 : !db.string
        %vStr2 = variant.create_scalar %str2 : !db.string
        %vStr3 = variant.create_scalar %str3 : !db.string

        %hLong5a = db.hash %vLong5a : !variant.variant
        %hLong5b = db.hash %vLong5b : !variant.variant
        %hLong6 = db.hash %vLong6 : !variant.variant
        %hDouble5 = db.hash %vDouble5 : !variant.variant
        %hBoolTrue = db.hash %vBoolTrue : !variant.variant
        %hLong1 = db.hash %vLong1 : !variant.variant
        %hStr1 = db.hash %vStr1 : !variant.variant
        %hStr2 = db.hash %vStr2 : !variant.variant
        %hStr3 = db.hash %vStr3 : !variant.variant

        // same-tag Long vs Long, equal value -> equal hash
        //CHECK: bool(true)
        %eqLong = arith.cmpi eq, %hLong5a, %hLong5b : index
        db.runtime_call "DumpValue" (%eqLong) : (i1) -> ()

        // same-tag Long vs Long, different value -> hash need not match
        // (asserting inequality here is a strong sanity check that the hash
        // isn't degenerate/constant, not part of the correctness contract)
        //CHECK: bool(false)
        %neqLong = arith.cmpi eq, %hLong5a, %hLong6 : index
        db.runtime_call "DumpValue" (%neqLong) : (i1) -> ()

        // cross-tag Long vs Double, numerically equal -> equal hash. This is
        // the central correctness assertion of the whole variant-hash change:
        // variant.cmp treats Long(5) == Double(5.0), so db.hash must too.
        //CHECK: bool(true)
        %eqCross = arith.cmpi eq, %hLong5a, %hDouble5 : index
        db.runtime_call "DumpValue" (%eqCross) : (i1) -> ()

        // cross-tag Boolean vs Long: Boolean is inside the "numeric family"
        // (see computeTagPredicates), and bool(true) == long(1) per
        // variant.cmp's numeric promotion -- equal hash required.
        //CHECK: bool(true)
        %eqBoolLong = arith.cmpi eq, %hBoolTrue, %hLong1 : index
        db.runtime_call "DumpValue" (%eqBoolLong) : (i1) -> ()

        // String, same value -> equal hash
        //CHECK: bool(true)
        %eqStr = arith.cmpi eq, %hStr1, %hStr2 : index
        db.runtime_call "DumpValue" (%eqStr) : (i1) -> ()

        // String, different value -> hash need not match (sanity check)
        //CHECK: bool(false)
        %neqStr = arith.cmpi eq, %hStr1, %hStr3 : index
        db.runtime_call "DumpValue" (%neqStr) : (i1) -> ()

        return
    }
}

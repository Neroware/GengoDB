// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// Same-tag add/sub/mul take the native arith fast path (no runtime call);
// div and cross-tag arithmetic fall back to a runtime call built on
// rdf4cpp's Literal arithmetic/promotion. An invalid combination (division
// by zero) yields a variant tagged Unspecified, the "no value" sentinel
// (see ArithOp's description in VariantOps.td). variant.variant_get_val is
// an unsafe unwrap with no defined behavior on a tag mismatch, so detecting
// that sentinel has to go through variant.variant_is_a instead of unwrapping.
module {
    func.func @main() {
        %i5 = arith.constant 5 : i64
        %i3 = arith.constant 3 : i64
        %i0 = arith.constant 0 : i64
        %f5 = arith.constant 5.0 : f64

        %v5 = variant.create_scalar %i5 : i64
        %v3 = variant.create_scalar %i3 : i64
        %v0 = variant.create_scalar %i0 : i64
        %vf5 = variant.create_scalar %f5 : f64

        // same-tag fast path
        %sum = variant.arith add %v5, %v3
        %sumVal = variant.variant_get_val %sum -> i64
        //CHECK: int(8)
        db.runtime_call "DumpValue" (%sumVal) : (i64) -> ()

        %prod = variant.arith mul %v5, %v3
        %prodVal = variant.variant_get_val %prod -> i64
        //CHECK: int(15)
        db.runtime_call "DumpValue" (%prodVal) : (i64) -> ()

        // cross-tag fallback: int + double
        %crossSum = variant.arith add %v5, %vf5
        %crossVal = variant.variant_get_val %crossSum -> f64
        //CHECK: float(10)
        db.runtime_call "DumpValue" (%crossVal) : (f64) -> ()

        // division by zero -> Unspecified: gengodb::semantics::xsd::Type's
        // explicit int32 for Unspecified is 0 (see Datatypes.h).
        %divByZero = variant.arith div %v5, %v0
        %unspecifiedTag = arith.constant 0 : i32
        %isUnspecified = variant.variant_is_a %divByZero { type = %unspecifiedTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isUnspecified) : (i1) -> ()

        return
    }
}

// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// Fixed-sized variant scalars (Int, Float, Date, Long,...) take arithmetic fast paths, 
// otherwise use arithemtic numeric cross via rdf4cpp API callbacks.
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

        // Same-tag fast path for a fixed-width numeric
        %i5_32 = arith.constant 5 : i32
        %i3_32 = arith.constant 3 : i32
        %v5_32 = variant.create_scalar %i5_32 : i32
        %v3_32 = variant.create_scalar %i3_32 : i32
        %sum32 = variant.arith add %v5_32, %v3_32
        %sum32Val = variant.variant_get_val %sum32 -> i32
        //CHECK: int(8)
        db.runtime_call "DumpValue" (%sum32Val) : (i32) -> ()

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

        // Same-tag *integer* division is excluded from the fast path on
        // purpose, because native `arith.divsi`/`divui` by zero traps.
        %i0_32 = arith.constant 0 : i32
        %v0_32 = variant.create_scalar %i0_32 : i32
        %divByZero32 = variant.arith div %v5_32, %v0_32
        %isUnspecified32 = variant.variant_is_a %divByZero32 { type = %unspecifiedTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isUnspecified32) : (i1) -> ()

        return
    }
}

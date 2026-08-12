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

        // xsd:Integer (arbitrary precision, blob-shaped; see
        // gengodb::semantics::xsd::Type::Integer = 200)
        %bigA = db.constant ("830103818230965482862112") : !db.string
        %bigB = db.constant ("1") : !db.string
        %vBigA = variant.create_scalar %bigA : !db.string { type = 200 }
        %vBigB = variant.create_scalar %bigB : !db.string { type = 200 }
        %bigSum = variant.arith add %vBigA, %vBigB
        %bigSumStr = variant.to_string %bigSum -> !db.string
        //CHECK: string("830103818230965482862113")
        db.runtime_call "DumpValue" (%bigSumStr) : (!db.string) -> ()

        // Cross-tag Integer + Long
        %iStr = db.constant ("700") : !db.string
        %vI = variant.create_scalar %iStr : !db.string { type = 200 }
        %l120 = arith.constant 120 : i64
        %vL120 = variant.create_scalar %l120 : i64
        %intLongCrossSum = variant.arith add %vI, %vL120
        %intLongCrossSumStr = variant.to_string %intLongCrossSum -> !db.string
        //CHECK: string("820")
        db.runtime_call "DumpValue" (%intLongCrossSumStr) : (!db.string) -> ()

        // xsd:Integer / xsd:Integer: per XSD/XPath op:numeric-divide, this
        // yields xsd:Decimal (tag 3), not xsd:Integer
        %iStr2 = db.constant ("7") : !db.string
        %vI2 = variant.create_scalar %iStr2 : !db.string { type = 200 }
        %iStr3 = db.constant ("2") : !db.string
        %vI3 = variant.create_scalar %iStr3 : !db.string { type = 200 }
        %divResult = variant.arith div %vI2, %vI3
        %decimalTag = arith.constant 3 : i32
        %isDecimalDiv = variant.variant_is_a %divResult { type = %decimalTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isDecimalDiv) : (i1) -> ()

        // xsd:Decimal + xsd:Integer cross-tag: symmetric to Integer/Long
        // above, exercising the other arbitrary-precision blob tag.
        %decStr = db.constant ("2.5") : !db.string
        %vDec = variant.create_scalar %decStr : !db.string { type = 3 }
        %decSum = variant.arith add %vDec, %vI2
        %isDecimalSum = variant.variant_is_a %decSum { type = %decimalTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isDecimalSum) : (i1) -> ()

        return
    }
}

// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

module {
    func.func @main() {
        %b = arith.constant 1 : i1
        %i = arith.constant 42 : i64
        %f = arith.constant 3.5 : f64

        %vb = variant.create_scalar %b : i1
        %vi = variant.create_scalar %i : i64
        %vf = variant.create_scalar %f : f64

        // matching-type unwrap succeeds
        %rb = variant.variant_get_val %vb -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%rb) : (!db.nullable<i1>) -> ()

        %ri = variant.variant_get_val %vi -> !db.nullable<i64>
        //CHECK: int(42)
        db.runtime_call "DumpValue" (%ri) : (!db.nullable<i64>) -> ()

        %rf = variant.variant_get_val %vf -> !db.nullable<f64>
        //CHECK: float(3.5)
        db.runtime_call "DumpValue" (%rf) : (!db.nullable<f64>) -> ()

        // mismatched-type unwrap -> null
        %mismatch = variant.variant_get_val %vi -> !db.nullable<f64>
        //CHECK: float(NULL)
        db.runtime_call "DumpValue" (%mismatch) : (!db.nullable<f64>) -> ()

        // Rest of the fixed-width inlined family create_scalar/variant_get_val
        // supports (see tagForScalarType's note on why the *unsigned* XSD
        // integer types aren't reachable this way -- only via create_ref).
        %byte = arith.constant -12 : i8
        %vByte = variant.create_scalar %byte : i8
        %rByte = variant.variant_get_val %vByte -> !db.nullable<i8>
        //CHECK: int(-12)
        db.runtime_call "DumpValue" (%rByte) : (!db.nullable<i8>) -> ()

        %short = arith.constant -1234 : i16
        %vShort = variant.create_scalar %short : i16
        %rShort = variant.variant_get_val %vShort -> !db.nullable<i16>
        //CHECK: int(-1234)
        db.runtime_call "DumpValue" (%rShort) : (!db.nullable<i16>) -> ()

        %int32 = arith.constant 123456 : i32
        %vInt32 = variant.create_scalar %int32 : i32
        %rInt32 = variant.variant_get_val %vInt32 -> !db.nullable<i32>
        //CHECK: int(123456)
        db.runtime_call "DumpValue" (%rInt32) : (!db.nullable<i32>) -> ()

        %f32 = arith.constant 2.5 : f32
        %vF32 = variant.create_scalar %f32 : f32
        %rF32 = variant.variant_get_val %vF32 -> !db.nullable<f32>
        //CHECK: float(2.5)
        db.runtime_call "DumpValue" (%rF32) : (!db.nullable<f32>) -> ()

        // String: variant_get_val returns !db.nullable<!db.string>, which
        // (unlike every other payload type here) lowers to a bare
        // `!util.varlen32` using its own invalid-sentinel encoding rather
        // than a {isNull, value} tuple -- unwrap with `db.nullable_get_val`
        // before dumping, matching how every other nullable-string consumer
        // in this compiler has to handle it.
        %str = db.constant ("hello") : !db.string
        %vStr = variant.create_scalar %str : !db.string
        %rStrNullable = variant.variant_get_val %vStr -> !db.nullable<!db.string>
        %rStr = db.nullable_get_val %rStrNullable : !db.nullable<!db.string>
        //CHECK: string("hello")
        db.runtime_call "DumpValue" (%rStr) : (!db.string) -> ()

        // mismatched-type unwrap on a string variant -> null, same as the
        // numeric mismatch case above.
        %strMismatch = variant.variant_get_val %vStr -> !db.nullable<i64>
        //CHECK: int(NULL)
        db.runtime_call "DumpValue" (%strMismatch) : (!db.nullable<i64>) -> ()

        return
    }
}

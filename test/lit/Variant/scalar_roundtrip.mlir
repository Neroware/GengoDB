// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// variant.variant_get_val is an unsafe, strict-tag-match unwrap (undefined
// behavior on a mismatch, see VariantOps.td) -- it never produces a null, so
// its result type is the plain payload type, never !db.nullable<...>.
// Checking whether a variant is safe to unwrap as a given type is a separate
// concern, covered by variant.variant_is_a below.
module {
    func.func @main() {
        %b = arith.constant 1 : i1
        %i = arith.constant 42 : i64
        %f = arith.constant 3.5 : f64

        %vb = variant.create_scalar %b : i1
        %vi = variant.create_scalar %i : i64
        %vf = variant.create_scalar %f : f64

        %rb = variant.variant_get_val %vb -> i1
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%rb) : (i1) -> ()

        %ri = variant.variant_get_val %vi -> i64
        //CHECK: int(42)
        db.runtime_call "DumpValue" (%ri) : (i64) -> ()

        %rf = variant.variant_get_val %vf -> f64
        //CHECK: float(3.5)
        db.runtime_call "DumpValue" (%rf) : (f64) -> ()

        // Rest of the fixed-width inlined family create_scalar/variant_get_val
        // supports (see tagForScalarType's note on why the *unsigned* XSD
        // integer types aren't reachable this way -- only via create_node_ref).
        %byte = arith.constant -12 : i8
        %vByte = variant.create_scalar %byte : i8
        %rByte = variant.variant_get_val %vByte -> i8
        //CHECK: int(-12)
        db.runtime_call "DumpValue" (%rByte) : (i8) -> ()

        %short = arith.constant -1234 : i16
        %vShort = variant.create_scalar %short : i16
        %rShort = variant.variant_get_val %vShort -> i16
        //CHECK: int(-1234)
        db.runtime_call "DumpValue" (%rShort) : (i16) -> ()

        %int32 = arith.constant 123456 : i32
        %vInt32 = variant.create_scalar %int32 : i32
        %rInt32 = variant.variant_get_val %vInt32 -> i32
        //CHECK: int(123456)
        db.runtime_call "DumpValue" (%rInt32) : (i32) -> ()

        %f32 = arith.constant 2.5 : f32
        %vF32 = variant.create_scalar %f32 : f32
        %rF32 = variant.variant_get_val %vF32 -> f32
        //CHECK: float(2.5)
        db.runtime_call "DumpValue" (%rF32) : (f32) -> ()

        // String: variant_get_val returns !db.string directly -- no nullable
        // wrapper, and (since it's a plain, unwrapped payload type here, not
        // routed through !db.nullable<!db.string>'s varlen32 sentinel
        // encoding) no separate `db.nullable_get_val` step needed either.
        %str = db.constant ("hello") : !db.string
        %vStr = variant.create_scalar %str : !db.string
        %rStr = variant.variant_get_val %vStr -> !db.string
        //CHECK: string("hello")
        db.runtime_call "DumpValue" (%rStr) : (!db.string) -> ()

        // variant.variant_is_a: tag membership check, no unwrap -- just
        // compares the packed tag against `typeId`. This is the safe way to
        // detect a mismatch (variant_get_val itself has none to offer). Tag
        // values are gengodb::semantics::xsd::Type's explicit int32s (see
        // Datatypes.h): Long = 203, Double = 5.
        %longTag = arith.constant 203 : i32
        %doubleTag = arith.constant 5 : i32
        %isLong = variant.variant_is_a %vi { type = %longTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isLong) : (i1) -> ()
        %isNotDouble = variant.variant_is_a %vi { type = %doubleTag }
        //CHECK: bool(false)
        db.runtime_call "DumpValue" (%isNotDouble) : (i1) -> ()

        return
    }
}

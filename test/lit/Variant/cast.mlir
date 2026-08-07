// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// variant.cast: converts a variant to a statically-known target XSD type
// (realizes SPARQL's xsd:TYPE(...) constructor-function casts). Same-tag
// casts are a lowering-level identity no-op; different-tag casts go through
// VariantRuntime::castLiteral (rdf4cpp::Literal::cast) and produce a variant
// tagged xsd::Type::Unspecified (int32 0) on failure -- the same "no value"
// sentinel variant.arith's lowering already uses. Tag values below are
// gengodb::semantics::xsd::Type's explicit int32s (see Datatypes.h):
// Boolean=2, Float=4, Double=5, Int=204, Short=205, Byte=206, Long=203.
module {
    func.func @main() {
        %unspecifiedTag = arith.constant 0 : i32
        %booleanTag = arith.constant 2 : i32
        %floatTag = arith.constant 4 : i32
        %doubleTag = arith.constant 5 : i32
        %intTag = arith.constant 204 : i32
        %byteTag = arith.constant 206 : i32
        %longTag = arith.constant 203 : i32

        // Same-type no-op: value round-trips exactly.
        %i64_42 = arith.constant 42 : i64
        %v_42 = variant.create_scalar %i64_42 : i64
        %sameType = variant.cast %v_42 { type = %longTag }
        %sameTypeVal = variant.variant_get_val %sameType -> i64
        //CHECK: int(42)
        db.runtime_call "DumpValue" (%sameTypeVal) : (i64) -> ()

        // Numeric widening: Int -> Long.
        %i32_42 = arith.constant 42 : i32
        %v_i32_42 = variant.create_scalar %i32_42 : i32
        %widened = variant.cast %v_i32_42 { type = %longTag }
        %widenedVal = variant.variant_get_val %widened -> i64
        //CHECK: int(42)
        db.runtime_call "DumpValue" (%widenedVal) : (i64) -> ()

        // Numeric narrowing, in range: Long -> Byte.
        %i64_100 = arith.constant 100 : i64
        %v_100 = variant.create_scalar %i64_100 : i64
        %narrowed = variant.cast %v_100 { type = %byteTag }
        %narrowedVal = variant.variant_get_val %narrowed -> i8
        //CHECK: int(100)
        db.runtime_call "DumpValue" (%narrowedVal) : (i8) -> ()

        // Numeric narrowing, out of range -> Unspecified.
        %i64_200 = arith.constant 200 : i64
        %v_200 = variant.create_scalar %i64_200 : i64
        %overflow = variant.cast %v_200 { type = %byteTag }
        %isUnspecifiedOverflow = variant.variant_is_a %overflow { type = %unspecifiedTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isUnspecifiedOverflow) : (i1) -> ()

        // Numeric -> boolean coercion.
        %i64_1 = arith.constant 1 : i64
        %v_1 = variant.create_scalar %i64_1 : i64
        %asBoolTrue = variant.cast %v_1 { type = %booleanTag }
        %asBoolTrueVal = variant.variant_get_val %asBoolTrue -> i1
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%asBoolTrueVal) : (i1) -> ()

        %i64_0 = arith.constant 0 : i64
        %v_0 = variant.create_scalar %i64_0 : i64
        %asBoolFalse = variant.cast %v_0 { type = %booleanTag }
        %asBoolFalseVal = variant.variant_get_val %asBoolFalse -> i1
        //CHECK: bool(false)
        db.runtime_call "DumpValue" (%asBoolFalseVal) : (i1) -> ()

        // Boolean -> numeric coercion (the reverse direction).
        %true = arith.constant 1 : i1
        %v_true = variant.create_scalar %true : i1
        %boolAsLong = variant.cast %v_true { type = %longTag }
        %boolAsLongVal = variant.variant_get_val %boolAsLong -> i64
        //CHECK: int(1)
        db.runtime_call "DumpValue" (%boolAsLongVal) : (i64) -> ()

        // String -> numeric, success.
        %str42 = db.constant ("42") : !db.string
        %v_str42 = variant.create_scalar %str42 : !db.string
        %strAsInt = variant.cast %v_str42 { type = %intTag }
        %strAsIntVal = variant.variant_get_val %strAsInt -> i32
        //CHECK: int(42)
        db.runtime_call "DumpValue" (%strAsIntVal) : (i32) -> ()

        // String -> numeric, failure -> Unspecified.
        %strHello = db.constant ("hello") : !db.string
        %v_strHello = variant.create_scalar %strHello : !db.string
        %strAsIntFail = variant.cast %v_strHello { type = %intTag }
        %isUnspecifiedStr = variant.variant_is_a %strAsIntFail { type = %unspecifiedTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isUnspecifiedStr) : (i1) -> ()

        // Float <-> Double cross-cast.
        %f32_25 = arith.constant 2.5 : f32
        %v_f32_25 = variant.create_scalar %f32_25 : f32
        %f32AsDouble = variant.cast %v_f32_25 { type = %doubleTag }
        %f32AsDoubleVal = variant.variant_get_val %f32AsDouble -> f64
        //CHECK: float(2.5)
        db.runtime_call "DumpValue" (%f32AsDoubleVal) : (f64) -> ()

        %f64_35 = arith.constant 3.5 : f64
        %v_f64_35 = variant.create_scalar %f64_35 : f64
        %f64AsFloat = variant.cast %v_f64_35 { type = %floatTag }
        %f64AsFloatVal = variant.variant_get_val %f64AsFloat -> f32
        //CHECK: float(3.5)
        db.runtime_call "DumpValue" (%f64AsFloatVal) : (f32) -> ()

        return
    }
}

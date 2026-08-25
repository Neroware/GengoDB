// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

// Fixed-sized variant scalars (Int, Float, Date, Long,...) take arithmetic fast paths, 
// otherwise use arithemtic numeric cross via rdf4cpp API callbacks.
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

        // Same-tag comparison for a fixed-width numeric tag xsd:unsignedByte.
        %by5 = arith.constant 5 : i8
        %by9 = arith.constant 9 : i8
        %vBy5 = variant.create_scalar %by5 : i8
        %vBy9 = variant.create_scalar %by9 : i8
        %ltByte = variant.cmp lt %vBy5, %vBy9 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%ltByte) : (!db.nullable<i1>) -> ()

        // Same-tag comparison for a fixed-width numeric tag xsd:int.
        %in5 = arith.constant 5 : i32
        %in9 = arith.constant 9 : i32
        %vIn5 = variant.create_scalar %in5 : i32
        %vIn9 = variant.create_scalar %in9 : i32
        %ltInt = variant.cmp lt %vIn5, %vIn9 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%ltInt) : (!db.nullable<i1>) -> ()

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

        // Same-tag xsd:Integer comparison (blob-shaped, canonicalized to a
        // scratch VarLen32)
        %int5 = db.constant ("5") : !db.string
        %int9 = db.constant ("9") : !db.string
        %vInt5 = variant.create_scalar %int5 : !db.string { type = 200 }
        %vInt9 = variant.create_scalar %int9 : !db.string { type = 200 }
        %eqInt = variant.cmp eq %vInt5, %vInt5 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%eqInt) : (!db.nullable<i1>) -> ()
        %ltInteger = variant.cmp lt %vInt5, %vInt9 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%ltInteger) : (!db.nullable<i1>) -> ()

        // AnyLiteralScalar (tag 1004)
        %al1 = util.varlen32_create_const "\0C\00\00\00http://ex/dt5"
        %al2 = util.varlen32_create_const "\0C\00\00\00http://ex/dt5"
        %al3 = util.varlen32_create_const "\0C\00\00\00http://ex/dt7"
        %al4 = util.varlen32_create_const "\0D\00\00\00http://ex/dtx5"
        %vAl1 = variant.create_scalar %al1 : !util.varlen32 { type = 1004 }
        %vAl2 = variant.create_scalar %al2 : !util.varlen32 { type = 1004 }
        %vAl3 = variant.create_scalar %al3 : !util.varlen32 { type = 1004 }
        %vAl4 = variant.create_scalar %al4 : !util.varlen32 { type = 1004 }

        // Same datatype IRI + same lexical form -> term-equal.
        %eqAlSame = variant.cmp eq %vAl1, %vAl2 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%eqAlSame) : (!db.nullable<i1>) -> ()

        // Same datatype IRI, different lexical form -> not term-equal.
        %eqAlDiffLex = variant.cmp eq %vAl1, %vAl3 -> !db.nullable<i1>
        //CHECK: bool(false)
        db.runtime_call "DumpValue" (%eqAlDiffLex) : (!db.nullable<i1>) -> ()
        %neqAlDiffLex = variant.cmp neq %vAl1, %vAl3 -> !db.nullable<i1>
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%neqAlDiffLex) : (!db.nullable<i1>) -> ()

        // Same lexical form, different datatype IRI -> not term-equal.
        %eqAlDiffDt = variant.cmp eq %vAl1, %vAl4 -> !db.nullable<i1>
        //CHECK: bool(false)
        db.runtime_call "DumpValue" (%eqAlDiffDt) : (!db.nullable<i1>) -> ()

        // Ordering operators on a non-XSD datatype are a SPARQL type error.
        %ltAlErr = variant.cmp lt %vAl1, %vAl3 -> !db.nullable<i1>
        //CHECK: bool(NULL)
        db.runtime_call "DumpValue" (%ltAlErr) : (!db.nullable<i1>) -> ()

        return
    }
}

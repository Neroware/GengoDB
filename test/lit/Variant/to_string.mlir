// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

module {
    func.func @main() {
        %b = arith.constant 1 : i1
        %i = arith.constant 42 : i64
        %f = arith.constant 3.5 : f64

        %vb = variant.create_scalar %b : i1
        %vi = variant.create_scalar %i : i64
        %vf = variant.create_scalar %f : f64

        %sb = variant.to_string %vb -> !db.string
        %si = variant.to_string %vi -> !db.string
        %sf = variant.to_string %vf -> !db.string

        //CHECK: string("true")
        db.runtime_call "DumpValue" (%sb) : (!db.string) -> ()
        //CHECK: string("42")
        db.runtime_call "DumpValue" (%si) : (!db.string) -> ()
        // XSD canonical form for xsd:double uses exponential notation.
        //CHECK: string("3.5E0")
        db.runtime_call "DumpValue" (%sf) : (!db.string) -> ()

        %f32 = arith.constant 21.5 : f32
        %vF32 = variant.create_scalar %f32 : f32
        %sF32 = variant.to_string %vF32 -> !db.string
        // xsd:float also uses exponential notation, same as xsd:double.
        //CHECK: string("2.15E1")
        db.runtime_call "DumpValue" (%sF32) : (!db.string) -> ()

        // String variants: to_string is a pure load of the already-stored
        // `!util.varlen32` (see ToStringOpLowering's `ifString` branch), not
        // a runtime call -- still exercised the same way as every other tag.
        %str = db.constant ("passthrough") : !db.string
        %vStr = variant.create_scalar %str : !db.string
        %sStr = variant.to_string %vStr -> !db.string
        //CHECK: string("passthrough")
        db.runtime_call "DumpValue" (%sStr) : (!db.string) -> ()

        // The "full" UnitAttr requests the full literal form
        %siFull = variant.to_string %vi -> !db.string {full}
        //CHECK: string(""42"^^<http://www.w3.org/2001/XMLSchema#long>")
        db.runtime_call "DumpValue" (%siFull) : (!db.string) -> ()

        %sStrFull = variant.to_string %vStr -> !db.string {full}
        //CHECK: string(""passthrough"")
        db.runtime_call "DumpValue" (%sStrFull) : (!db.string) -> ()

        // AnyLiteralScalar (tag 1004)
        %anyLit = util.varlen32_create_const "\0C\00\00\00http://ex/dt5"
        %vAnyLit = variant.create_scalar %anyLit : !util.varlen32 { type = 1004 }
        %sAnyLit = variant.to_string %vAnyLit -> !db.string
        //CHECK: string("5")
        db.runtime_call "DumpValue" (%sAnyLit) : (!db.string) -> ()

        %sAnyLitFull = variant.to_string %vAnyLit -> !db.string {full}
        //CHECK: string(""5"^^<http://ex/dt>")
        db.runtime_call "DumpValue" (%sAnyLitFull) : (!db.string) -> ()

        return
    }
}

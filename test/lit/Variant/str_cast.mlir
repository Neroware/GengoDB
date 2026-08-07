// RUN: mlir-db-opt --lower-variant-to-std %s | env LINGODB_EXECUTION_MODE=DEFAULT run-mlir - | FileCheck %s

module {
    func.func @main() {
        %stringTag = arith.constant 1 : i32

        // Numeric -> string.
        %i64_42 = arith.constant 42 : i64
        %v_42 = variant.create_scalar %i64_42 : i64
        %casted = variant.str_cast %v_42
        %isString = variant.variant_is_a %casted { type = %stringTag }
        //CHECK: bool(true)
        db.runtime_call "DumpValue" (%isString) : (i1) -> ()
        %castedStr = variant.to_string %casted -> !db.string
        //CHECK: string("42")
        db.runtime_call "DumpValue" (%castedStr) : (!db.string) -> ()

        // String passthrough
        %strAlready = db.constant ("already") : !db.string
        %v_strAlready = variant.create_scalar %strAlready : !db.string
        %castedAlready = variant.str_cast %v_strAlready
        %castedAlreadyStr = variant.to_string %castedAlready -> !db.string
        //CHECK: string("already")
        db.runtime_call "DumpValue" (%castedAlreadyStr) : (!db.string) -> ()

        return
    }
}

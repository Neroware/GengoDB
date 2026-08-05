add_mlir_dialect_library(MLIRGraphSubOp
        GraphSubOpDialect.cpp
        GraphSubOps.cpp
        GraphSubOpsEnums.cpp
        GraphSubOpTypes.cpp
        GraphSubOpAttributes.cpp
        NamedGraphManager.cpp
        Transforms/StringifyVariants.cpp
        Transforms/Passes.cpp
        ADDITIONAL_HEADER_DIRS
        ../../../../../include/gengodb/compiler/Dialect/GraphSubOp

        DEPENDS
        MLIRGraphSubOpsIncGen
        MLIRSubOperatorOpsIncGen
        MLIRDBOpsIncGen
        MLIRVariantOpsIncGen

        LINK_LIBS mlir-support PUBLIC
        MLIRIR
        MLIRVariant
        runtime_funcs_ptr
)
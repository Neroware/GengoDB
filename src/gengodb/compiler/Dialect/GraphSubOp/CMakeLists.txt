add_mlir_dialect_library(MLIRGraphSubOp
        GraphSubOpDialect.cpp
        GraphSubOps.cpp
        GraphSubOpsEnums.cpp
        GraphSubOpTypes.cpp
        GraphSubOpAttributes.cpp
        NamedGraphManager.cpp
        ADDITIONAL_HEADER_DIRS
        ../../../../../include/gengodb/compiler/Dialect/GraphSubOp

        DEPENDS
        MLIRGraphSubOpsIncGen
        MLIRSubOperatorOpsIncGen
        MLIRDBOpsIncGen

        LINK_LIBS mlir-support PUBLIC
        MLIRIR
        runtime_funcs_ptr
)
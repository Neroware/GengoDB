add_mlir_dialect_library(MLIRXSD
        XSDDialect.cpp
        XSDOps.cpp
        XSDOpsEnums.cpp
        ADDITIONAL_HEADER_DIRS
        ../../../../../include/gengodb/compiler/Dialect/XSD

        DEPENDS
        MLIRXSDOpsIncGen
        MLIRGraphSubOpsIncGen

        LINK_LIBS mlir-support PUBLIC
        MLIRIR
        MLIRGraphSubOp
)

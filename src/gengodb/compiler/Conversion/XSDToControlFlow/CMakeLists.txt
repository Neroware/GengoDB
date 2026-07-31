add_mlir_conversion_library(MLIRXSDToControlFlow
        XSDToControlFlow.cpp

        ADDITIONAL_HEADER_DIRS
        ../../../../../include/gengodb/compiler/Conversion/XSDToControlFlow

        DEPENDS
        xsd-rt-defs

        LINK_COMPONENTS
        Core

        LINK_LIBS PUBLIC
        MLIRIR
        MLIRXSD
        MLIRGraphSubOp
        MLIRDB
        MLIRUtil
        runtime_funcs_ptr
)

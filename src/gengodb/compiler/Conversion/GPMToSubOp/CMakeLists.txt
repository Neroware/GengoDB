add_mlir_conversion_library(MLIRGPMToSubOp
        GPMToSubOp.cpp

        ADDITIONAL_HEADER_DIRS
        ../../../lingodb/compiler/Conversion/GPMToSubOp

        DEPENDS
        LINK_COMPONENTS
        Core

        LINK_LIBS PUBLIC
        MLIRIR
        MLIRGPM
        MLIRSubOperator
        runtime_funcs_ptr
)

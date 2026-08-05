add_mlir_conversion_library(MLIRVariantToStd
        VariantToStd.cpp

        ADDITIONAL_HEADER_DIRS
        ../../../../../include/gengodb/compiler/Conversion/VariantToStd

        DEPENDS
        variant-rt-defs

        LINK_COMPONENTS
        Core

        LINK_LIBS PUBLIC
        MLIRIR
        MLIRVariant
        MLIRDB
        MLIRUtil
        runtime_funcs_ptr
)

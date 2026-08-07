add_mlir_conversion_library(MLIRDBToArrowStd
        LowerToStd.cpp

        ADDITIONAL_HEADER_DIRS
        ../../../mlir/Conversion/DBToArrowStd

        DEPENDS
        dump-rt-defs
        string-rt-defs
        list-rt-defs
        date-rt-defs
        decimal-rt-defs
        ht-rt-defs
        variant-rt-defs
        LINK_COMPONENTS
        Core

        LINK_LIBS mlir-support PUBLIC
        MLIRIR
        MLIRDB
        MLIRSCFTransforms
        MLIRVariant
        runtime_funcs_ptr
)

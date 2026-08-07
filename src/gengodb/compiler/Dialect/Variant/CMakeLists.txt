add_mlir_dialect_library(MLIRVariant
        VariantDialect.cpp
        VariantTypes.cpp
        VariantOps.cpp
        VariantOpsEnums.cpp
        ADDITIONAL_HEADER_DIRS
        ../../../../../include/gengodb/compiler/Dialect/Variant

        DEPENDS
        MLIRVariantOpsIncGen

        LINK_LIBS mlir-support PUBLIC
        MLIRIR
)

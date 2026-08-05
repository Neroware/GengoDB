add_mlir_dialect_library(MLIRGPM
        GPMDialect.cpp
        GPMOpsInterfaceImpl.cpp
        GPMOps.cpp
        GPMEnums.cpp
        GPMTypes.cpp
        GPMAttributes.cpp
        # Transforms/UnnestGraphPatterns.cpp
        # Transforms/CreateRelAlgInFlights.cpp
        # Transforms/Passes.cpp
        ADDITIONAL_HEADER_DIRS
        ../../../../../include/gengodb/compiler/Dialect/GPM

        DEPENDS
        MLIRGPMOpsIncGen
        MLIRGraphSubOpsIncGen
        MLIRSubOperatorOpsIncGen
        MLIRDBOpsIncGen

        LINK_LIBS mlir-support PUBLIC
        MLIRIR
        runtime_funcs_ptr
)
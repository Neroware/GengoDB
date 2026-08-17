cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

# the official Blake3 implementation does not contain a usable CMakeLists.txt
# (only one for running test cases)

# respect C_EXTENSIONS OFF without explicitly setting C_STANDARD
if (POLICY CMP0128)
  cmake_policy(SET CMP0128 NEW)
endif()

project(libblake3
        VERSION 1.5.4
        DESCRIPTION "BLAKE3 C implementation"
        LANGUAGES C ASM
)

include(FeatureSummary)
include(GNUInstallDirs)

set(blake3_rev 95e42b84fc4709974c7b23c7ae885989ab36c31e)
set(blake3_files
        blake3.c|82584e94af37ed895b43159b359845879a49b9f792c17828538e51a5b28db5ac
        blake3.h|7e6022d0fe716bc1695d71c9b0e7fccd35b99f5c7d7f7f5804430cc303ae99bf
        blake3-config.cmake.in|4ee7decdc9d25f292fac03021bbe6e2611e2186c418f51fc52db6445e28a8cb8
        blake3_avx2.c|8b3516a8e42d981a9868ef670b1698f6d10207283086bd810b11cdaac1aca8f1
        blake3_avx2_x86-64_unix.S|a2d95519fd9845cf80252110e21a93124e81c03da836eaac0bca0537be4f8324
        blake3_avx2_x86-64_windows_gnu.S|c5b5e561f8fbbd252492a52f9a09da8e9e99a6ecccb88e2d1bfa2ebc7961f425
        blake3_avx2_x86-64_windows_msvc.asm|2aad514ac8fb748accc8c0c0426606c91e6d404cff9c8499b2052e8108cbf67a
        blake3_avx512.c|46653e718211d883e56d5caa9ded6d9450f2f5b44ebfa81372b3c0be538e9e3c
        blake3_avx512_x86-64_unix.S|c641987dd82fabed59d527f1a3413c4a2ff3df919af6d13ff2788c95e4035c0d
        blake3_avx512_x86-64_windows_gnu.S|b5c94f9201ab86dc1d5170b43aea440fe2a7cdbe2bec03fa1efd7c16dde1ddfb
        blake3_avx512_x86-64_windows_msvc.asm|10a46aee705ad10a41db286c56030e3aa2456d293272029f6e5a71b218d4aac3
        blake3_dispatch.c|6cab4b80158d02ec5c65351c25850926d13ec60216d6cc597deca6f4b17b6c0f
        blake3_impl.h|7a801b6a09e9a2fc35f7520ef584d22d70521867ab701f25ccf0409409dfe8f1
        blake3_neon.c|766c91fac5262292cb723d4512ca1652a43f3f7f80a5fedce71533dd7047ede3
        blake3_portable.c|2bc25b0dad67b4329d0b49cfa075ab2b0d04e424addbddc4e9c389c52a192524
        blake3_sse2.c|51194fff1b0e9cb79f4db6fc148a3b34f683ca6ec3a260d66166e0f7d5b7012e
        blake3_sse2_x86-64_unix.S|94baa998f4114bf57b99e1791a21e9f0bcdc4bd5803864eb30a2477440b7380c
        blake3_sse2_x86-64_windows_gnu.S|cf719d08059efee4bafecea32ef8fba46ed1f41f60782461f5047046c5e405ec
        blake3_sse2_x86-64_windows_msvc.asm|27bf95d5611dca1530f85106748208a6d7a68f9a4f7f61670aa680bcc9a5e436
        blake3_sse41.c|1f38e2ca58aa59af094f492f1326957b423d7a25e32daaae159fcc1966949f58
        blake3_sse41_x86-64_unix.S|135048e4a81a6f9e86960b115141874a55954dbfd2cd1e8e9818837b46b498ad
        blake3_sse41_x86-64_windows_gnu.S|f41da21f411bbf3920feb9e5e34e3fcc66274b02edb930882eeae2d0aba74a08
        blake3_sse41_x86-64_windows_msvc.asm|c3e218899b6b52e22c1d552392bf972e7ac513addc2c247635cf60325dc39724
        libblake3.pc.in|c7e6551baaa05aa0978dfcdf1291b3b5901d67868736f98ef3dd74cdc1fe78dc
)
set(blake3_license_files
        LICENSE_A2|00fcc7a934ddbc9ece2a7cc063ac788e284b703b1d705ccbba72d462aa97921e
        LICENSE_A2LLVM|a5695f57ea0c221e0e8b7d784ff774c35e88c3d3270353646a925880bb3492cc
        LICENSE_CC0|a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499
)

include(FetchContent)
FetchContent_Declare(
        blake3_upstream
        GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
        GIT_TAG ${blake3_rev}
)
FetchContent_Populate(blake3_upstream)

function(blake3_stage file expected_hash subdir)
  set(src "${blake3_upstream_SOURCE_DIR}/${subdir}${file}")
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "BLAKE3 clone is missing expected file: ${src}")
  endif()
  file(SHA256 "${src}" actual_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(FATAL_ERROR "BLAKE3 file ${file} hash mismatch (got ${actual_hash}, expected ${expected_hash}) at rev ${blake3_rev}")
  endif()
  configure_file("${src}" "${CMAKE_CURRENT_BINARY_DIR}/src/blake3/${file}" COPYONLY)
endfunction()

foreach(entry ${blake3_files})
  string(REPLACE "|" ";" entry "${entry}")
  list(GET entry 0 file)
  list(GET entry 1 hash)
  blake3_stage("${file}" "${hash}" "c/")
endforeach()
foreach(entry ${blake3_license_files})
  string(REPLACE "|" ";" entry "${entry}")
  list(GET entry 0 file)
  list(GET entry 1 hash)
  blake3_stage("${file}" "${hash}" "")
endforeach()

# architecture lists for which to enable assembly / SIMD sources
set(BLAKE3_AMD64_NAMES amd64 AMD64 x86_64)
set(BLAKE3_X86_NAMES i686 x86 X86)
set(BLAKE3_ARMv8_NAMES aarch64 AArch64 arm64 ARM64 armv8 armv8a)
# default SIMD compiler flag configuration (can be overriden by toolchains or CLI)
if(MSVC)
  set(BLAKE3_CFLAGS_SSE2 "/arch:SSE2" CACHE STRING "the compiler flags to enable SSE2")
  # MSVC has no dedicated sse4.1 flag (see https://learn.microsoft.com/en-us/cpp/build/reference/arch-x86?view=msvc-170)
  set(BLAKE3_CFLAGS_SSE4.1 "/arch:AVX" CACHE STRING "the compiler flags to enable SSE4.1")
  set(BLAKE3_CFLAGS_AVX2 "/arch:AVX2" CACHE STRING "the compiler flags to enable AVX2")
  set(BLAKE3_CFLAGS_AVX512 "/arch:AVX512" CACHE STRING "the compiler flags to enable AVX512")

elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_C_COMPILER_ID STREQUAL "Clang"
        OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
  set(BLAKE3_CFLAGS_SSE2 "-msse2" CACHE STRING "the compiler flags to enable SSE2")
  set(BLAKE3_CFLAGS_SSE4.1 "-msse4.1" CACHE STRING "the compiler flags to enable SSE4.1")
  set(BLAKE3_CFLAGS_AVX2 "-mavx2" CACHE STRING "the compiler flags to enable AVX2")
  set(BLAKE3_CFLAGS_AVX512 "-mavx512f -mavx512vl" CACHE STRING "the compiler flags to enable AVX512")

  if (CMAKE_SYSTEM_PROCESSOR IN_LIST BLAKE3_ARMv8_NAMES
          AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 32-bit ARMv8 needs NEON to be enabled explicitly
    set(BLAKE3_CFLAGS_NEON "-mfpu=neon" CACHE STRING "the compiler flags to enable NEON")
  endif()
endif()

# library target
add_library(blake3
        ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3.c
        ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_dispatch.c
        ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_portable.c
)
add_library(BLAKE3::blake3 ALIAS blake3)

# library configuration
set(BLAKE3_PKGCONFIG_CFLAGS)
if (BUILD_SHARED_LIBS)
  target_compile_definitions(blake3
          PUBLIC BLAKE3_DLL
          PRIVATE BLAKE3_DLL_EXPORTS
  )
  list(APPEND BLAKE3_PKGCONFIG_CFLAGS -DBLAKE3_DLL)
endif()
target_include_directories(blake3 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src/blake3/>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(blake3 PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 0
        C_VISIBILITY_PRESET hidden
        C_EXTENSIONS OFF
)
target_compile_features(blake3 PUBLIC c_std_99)
# ensure C_EXTENSIONS OFF is respected without overriding CMAKE_C_STANDARD
# which may be set by the user or toolchain file
if (NOT POLICY CMP0128 AND NOT DEFINED CMAKE_C_STANDARD)
  set_target_properties(blake3 PROPERTIES C_STANDARD 99)
endif()

# optional SIMD sources
macro(BLAKE3_DISABLE_SIMD)
  set(BLAKE3_SIMD_AMD64_ASM OFF)
  set(BLAKE3_SIMD_X86_INTRINSICS OFF)
  set(BLAKE3_SIMD_NEON_INTRINSICS OFF)
  target_compile_definitions(blake3 PRIVATE
          BLAKE3_USE_NEON=0
          BLAKE3_NO_SSE2
          BLAKE3_NO_SSE41
          BLAKE3_NO_AVX2
          BLAKE3_NO_AVX512
  )
endmacro()

if(CMAKE_SYSTEM_PROCESSOR IN_LIST BLAKE3_AMD64_NAMES OR BLAKE3_USE_AMD64_ASM)
  set(BLAKE3_SIMD_AMD64_ASM ON)

  if(MSVC)
    enable_language(ASM_MASM)
    target_sources(blake3 PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx2_x86-64_windows_msvc.asm
            ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx512_x86-64_windows_msvc.asm
            ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse2_x86-64_windows_msvc.asm
            ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse41_x86-64_windows_msvc.asm
    )

  elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU"
          OR CMAKE_C_COMPILER_ID STREQUAL "Clang"
          OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    if (WIN32)
      target_sources(blake3 PRIVATE
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx2_x86-64_windows_gnu.S
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx512_x86-64_windows_gnu.S
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse2_x86-64_windows_gnu.S
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse41_x86-64_windows_gnu.S
      )

    elseif(UNIX)
      target_sources(blake3 PRIVATE
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx2_x86-64_unix.S
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx512_x86-64_unix.S
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse2_x86-64_unix.S
              ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse41_x86-64_unix.S
      )

    else()
      BLAKE3_DISABLE_SIMD()
    endif()

  else()
    BLAKE3_DISABLE_SIMD()
  endif()

elseif((CMAKE_SYSTEM_PROCESSOR IN_LIST BLAKE3_X86_NAMES OR BLAKE3_USE_X86_INTRINSICS)
        AND DEFINED BLAKE3_CFLAGS_SSE2
        AND DEFINED BLAKE3_CFLAGS_SSE4.1
        AND DEFINED BLAKE3_CFLAGS_AVX2
        AND DEFINED BLAKE3_CFLAGS_AVX512)
  set(BLAKE3_SIMD_X86_INTRINSICS ON)

  target_sources(blake3 PRIVATE
          ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx2.c
          ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_avx512.c
          ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse2.c
          ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_sse41.c
  )
  set_source_files_properties(blake3_avx2.c PROPERTIES COMPILE_FLAGS "${BLAKE3_CFLAGS_AVX2}")
  set_source_files_properties(blake3_avx512.c PROPERTIES COMPILE_FLAGS "${BLAKE3_CFLAGS_AVX512}")
  set_source_files_properties(blake3_sse2.c PROPERTIES COMPILE_FLAGS "${BLAKE3_CFLAGS_SSE2}")
  set_source_files_properties(blake3_sse41.c PROPERTIES COMPILE_FLAGS "${BLAKE3_CFLAGS_SSE4.1}")

elseif((CMAKE_SYSTEM_PROCESSOR IN_LIST BLAKE3_ARMv8_NAMES
        OR ANDROID_ABI STREQUAL "armeabi-v7a"
        OR BLAKE3_USE_NEON_INTRINSICS)
        AND (DEFINED BLAKE3_CFLAGS_NEON
        OR CMAKE_SIZEOF_VOID_P EQUAL 8))
  set(BLAKE3_SIMD_NEON_INTRINSICS ON)

  target_sources(blake3 PRIVATE
          ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3_neon.c
  )
  target_compile_definitions(blake3 PRIVATE
          BLAKE3_USE_NEON=1
  )

  if (DEFINED BLAKE3_CFLAGS_NEON)
    set_source_files_properties(blake3_neon.c PROPERTIES COMPILE_FLAGS "${BLAKE3_CFLAGS_NEON}")
  endif()

else()
  BLAKE3_DISABLE_SIMD()
endif()

# cmake install support
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/LICENSE_A2
        ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/LICENSE_A2LLVM
        ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/LICENSE_CC0
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses"
)
install(TARGETS blake3 EXPORT blake3-targets)
install(EXPORT blake3-targets
        NAMESPACE BLAKE3::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/blake3"
)

include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_BINARY_DIR}/src/blake3/blake3-config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/blake3-config.cmake"

        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/blake3"
)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/blake3-config-version.cmake"
        VERSION ${libblake3_VERSION}
        COMPATIBILITY SameMajorVersion
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/blake3-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/blake3-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/blake3"
)

configure_file(${CMAKE_CURRENT_BINARY_DIR}/src/blake3/libblake3.pc.in ${CMAKE_CURRENT_BINARY_DIR}/src/blake3/libblake3.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/src/blake3/libblake3.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# print feature summary
add_feature_info("AMD64 assembly" BLAKE3_SIMD_AMD64_ASM "The library uses hand written amd64 SIMD assembly.")
add_feature_info("x86 SIMD intrinsics" BLAKE3_SIMD_X86_INTRINSICS "The library uses x86 SIMD intrinsics.")
add_feature_info("NEON SIMD intrinsics" BLAKE3_SIMD_NEON_INTRINSICS "The library uses NEON SIMD intrinsics.")
feature_summary(WHAT ENABLED_FEATURES)

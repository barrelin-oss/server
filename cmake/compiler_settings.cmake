# compiler_settings.cmake
# C++20 compiler settings for Helbreath server

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific settings
if(MSVC)
    # MSVC settings
    add_compile_options(
        /MP                     # Multi-processor compilation
        /W4                     # Warning level 4
        /permissive-            # Standards conformance
        /Zc:__cplusplus         # Correct __cplusplus macro
        /Zc:preprocessor        # Standards-conforming preprocessor
        /utf-8                  # UTF-8 source and execution charset
    )

    # Release optimizations
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
        add_link_options(/LTCG)
    endif()

    # Disable specific warnings for legacy code compatibility
    add_compile_options(
        /wd4189                 # Local variable initialized but not referenced
        /wd4996                 # Deprecated functions (strcpy, etc.)
        /w14456                 # Declaration hides previous local (shadow)
        /w14457                 # Declaration hides function parameter (shadow)
        /w14244                 # Conversion, possible loss of data
        /w14245                 # Signed/unsigned mismatch in conversion
    )

else()
    # GCC/Clang settings
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wno-deprecated-declarations
        -Wno-missing-field-initializers  # C++20 designated initializers value-initialize omitted fields
    )

    # Release optimizations
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -flto)
        add_link_options(-flto)
    endif()
endif()

# Define debug/release macros
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG _DEBUG)
else()
    add_compile_definitions(NDEBUG)
endif()

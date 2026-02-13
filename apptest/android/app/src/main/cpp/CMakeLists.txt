cmake_minimum_required(VERSION 3.22.1)
project("apptest_android")

set(AROMA_ROOT "/home/yassineaa/AromaUI")

# 1. Setup FreeType
set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
add_subdirectory(${AROMA_ROOT}/vendors/freetype freetype_build)

# 2. Setup AromaUI
add_subdirectory(${AROMA_ROOT}/src aroma_lib)

# 3. Setup Android Glue
# We compile glue directly into the shared lib to ensure the entry point symbol is exported
set(ANDROID_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
set(ANDROID_GLUE_SRC ${ANDROID_GLUE_DIR}/android_native_app_glue.c)

# 4. Main App
add_library(aroma_app SHARED ../../../../../src/main.c ${ANDROID_GLUE_SRC})

target_include_directories(aroma_app PRIVATE ${AROMA_ROOT}/include ${ANDROID_GLUE_DIR})

target_link_libraries(aroma_app android aroma log)

cmake_minimum_required(VERSION 3.15)
project(settings_app C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG -pipe")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -pipe")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "")
set(CMAKE_STRIP_FLAGS "--strip-all")

option(ENABLE_VOICE_CONTROL "Enable voice control (requires libvosk)" OFF)
option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)
option(ENABLE_BLUETOOTH_SPEAKER "Enable Bluetooth speaker functionality" ON)
option(ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer instrumentation" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer instrumentation (mutually exclusive with ENABLE_ASAN)" OFF)
option(ENABLE_DEBUG_SYMBOLS "Keep debug symbols in release build" OFF)
option(ENABLE_GPU_HARDWARE "Force GPU hardware acceleration" ON)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive - ASan and TSan cannot be combined in one binary.")
endif()

if((ENABLE_ASAN OR ENABLE_TSAN) AND EMSCRIPTEN)
    message(WARNING "Sanitizers are not supported for Emscripten builds via this flag - disabling.")
    set(ENABLE_ASAN OFF)
    set(ENABLE_TSAN OFF)
endif()

set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer -g -O1)
    set(SANITIZER_LINK_FLAGS -fsanitize=address)
    message(STATUS "Sanitizer: AddressSanitizer + LeakSanitizer ENABLED")
elseif(ENABLE_TSAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SANITIZER_COMPILE_FLAGS -fsanitize=thread -fno-omit-frame-pointer -g -O1)
    set(SANITIZER_LINK_FLAGS -fsanitize=thread)
    message(STATUS "Sanitizer: ThreadSanitizer ENABLED")
else()
    message(STATUS "Sanitizer: DISABLED")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT ENABLE_DEBUG_SYMBOLS)
        set(PRODUCTION_COMPILE_FLAGS 
            -fno-omit-frame-pointer
            -flto=auto
        )
        set(PRODUCTION_LINK_FLAGS 
            -flto=auto
        )
        message(STATUS "Production build: LTO enabled, symbols stripped")
    else()
        set(PRODUCTION_COMPILE_FLAGS 
            -g
            -flto=auto
        )
        set(PRODUCTION_LINK_FLAGS 
            -flto=auto
        )
        message(STATUS "Production build: Debug symbols kept")
    endif()
else()
    set(PRODUCTION_COMPILE_FLAGS 
        -g
    )
    set(PRODUCTION_LINK_FLAGS "")
endif()

if(NOT TARGET aroma)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aroma_root)
endif()

target_compile_options(aroma PRIVATE 
    ${PRODUCTION_COMPILE_FLAGS}
    ${SANITIZER_COMPILE_FLAGS}
)

target_link_options(aroma PRIVATE
    ${PRODUCTION_LINK_FLAGS}
    ${SANITIZER_LINK_FLAGS}
)

if(ENABLE_BLUETOOTH_SPEAKER AND NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS REQUIRED dbus-1)
    pkg_check_modules(PULSE REQUIRED libpulse)
    if(NOT DBUS_FOUND)
        message(FATAL_ERROR "D-Bus development package not found. Install libdbus-1-dev (Ubuntu) or dbus-devel (Fedora)")
    endif()
    if(NOT PULSE_FOUND)
        message(FATAL_ERROR "PulseAudio development package not found. Install libpulse-dev (Ubuntu) or pulseaudio-libs-devel (Fedora)")
    endif()
endif()

add_executable(infotainment
        main.c 
       main_loop.c 
       app_state.c 
       voice_handler.c 
       theme_manager.c 
       font_manager.c 
       ui_animation_utils.c 
       vehicle_view.c 
       settings_ui.c 
       easter_egg.c 
       status_bar.c 
       tabs_manager.c
       bt_speaker_api.c
       bt_speaker_hfp.c
)

target_compile_options(infotainment PRIVATE
    ${PRODUCTION_COMPILE_FLAGS}
    ${SANITIZER_COMPILE_FLAGS}
)

target_link_options(infotainment PRIVATE
    ${PRODUCTION_LINK_FLAGS}
    ${SANITIZER_LINK_FLAGS}
)

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ENABLE_DEBUG_SYMBOLS)
    target_link_options(infotainment PRIVATE
        -Wl,--gc-sections
        -Wl,--strip-all
        -Wl,--as-needed
    )
endif()

if(NOT EMSCRIPTEN AND NOT ENABLE_GLFW AND ENABLE_GPU_HARDWARE)
    target_compile_definitions(infotainment PRIVATE
        AROMA_FORCE_HARDWARE_GPU=1
    )
    target_compile_definitions(aroma PRIVATE 
        GLPS_USE_EGL=1
        GLPS_USE_WAYLAND=1
        AROMA_FORCE_HARDWARE_GPU=1
    )
    target_link_options(infotainment PRIVATE
        -Wl,--no-as-needed
        -ldl
    )
    target_link_options(aroma PRIVATE
        -Wl,--no-as-needed
        -ldl
    )
    message(STATUS "GPU Hardware Acceleration: FORCED")
endif()

if(ENABLE_VOICE_CONTROL)
    find_library(VOSK_LIB vosk HINTS "${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib")
    if(NOT VOSK_LIB)
        message(WARNING "libvosk not found - disabling voice control")
        set(ENABLE_VOICE_CONTROL OFF)
    endif()
endif()

if(ENABLE_VOICE_CONTROL)
    target_sources(infotainment PRIVATE voice_control.c)
    target_compile_definitions(infotainment PRIVATE AROMA_USE_VOICE_CONTROL)
    target_link_libraries(infotainment PRIVATE vosk)
    message(STATUS "Voice control: ENABLED")
else()
    message(STATUS "Voice control: DISABLED")
endif()

target_include_directories(infotainment PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib"
)

if(ENABLE_BLUETOOTH_SPEAKER AND NOT EMSCRIPTEN)
    target_include_directories(infotainment PRIVATE
        ${DBUS_INCLUDE_DIRS}
        ${PULSE_INCLUDE_DIRS}
    )
    target_compile_definitions(infotainment PRIVATE 
        BT_SPEAKER_ENABLED
        _GNU_SOURCE
    )
    target_compile_options(infotainment PRIVATE
        ${PRODUCTION_COMPILE_FLAGS}
        ${DBUS_CFLAGS_OTHER}
        ${PULSE_CFLAGS_OTHER}
    )
endif()

if(EMSCRIPTEN)
    set_target_properties(infotainment PROPERTIES SUFFIX ".html")
    target_link_libraries(infotainment PRIVATE aroma)
    target_link_options(infotainment PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
        "-sFETCH=1"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )
    if(ENABLE_BLUETOOTH_SPEAKER)
        message(WARNING "Bluetooth speaker not available for Emscripten builds - disabling")
        set(ENABLE_BLUETOOTH_SPEAKER OFF)
    endif()
elseif(ENABLE_GLFW)
    target_link_libraries(infotainment PRIVATE
        aroma
        m
        pthread
        asound
        curl
        dbus-1
    )
    message(STATUS "Windowing Backend: GLFW (Native)")
else()
    target_link_libraries(infotainment PRIVATE
        aroma
        m
        pthread
        asound
        curl
    )
    message(STATUS "Windowing Backend: GLPS (Native)")
endif()

if(ENABLE_BLUETOOTH_SPEAKER AND NOT EMSCRIPTEN)
    target_link_libraries(infotainment PRIVATE
        ${DBUS_LIBRARIES}
        ${PULSE_LIBRARIES}
    )
    message(STATUS "Bluetooth Speaker: ENABLED")
else()
    message(STATUS "Bluetooth Speaker: DISABLED")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ENABLE_DEBUG_SYMBOLS)
    add_custom_command(TARGET infotainment POST_BUILD
        COMMAND ${CMAKE_STRIP} ${CMAKE_STRIP_FLAGS} $<TARGET_FILE:infotainment>
        COMMENT "Stripping debug symbols from infotainment binary"
        VERBATIM
    )
endif()

message(STATUS "")
message(STATUS "=== Build Configuration Summary ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Windowing Backend: ${ENABLE_GLFW}")
message(STATUS "Voice Control: ${ENABLE_VOICE_CONTROL}")
message(STATUS "Bluetooth Speaker: ${ENABLE_BLUETOOTH_SPEAKER}")
message(STATUS "GPU Hardware: ${ENABLE_GPU_HARDWARE}")
message(STATUS "Debug Symbols: ${ENABLE_DEBUG_SYMBOLS}")
message(STATUS "Sanitizers: ${ENABLE_ASAN} / ${ENABLE_TSAN}")
message(STATUS "LTO Enabled: ${CMAKE_BUILD_TYPE}==Release")
message(STATUS "=====================================")
message(STATUS "")
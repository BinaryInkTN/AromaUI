cmake_minimum_required(VERSION 3.15)
project(settings_app C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_VOICE_CONTROL "Enable voice control (requires libvosk)" OFF)
option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)
option(ENABLE_BLUETOOTH_SPEAKER "Enable Bluetooth speaker functionality" ON)
option(ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer instrumentation" ON)
option(ENABLE_TSAN "Build with ThreadSanitizer instrumentation (mutually exclusive with ENABLE_ASAN)" OFF)

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

# Sanitizer flag sets - defined once, applied everywhere below so the
# instrumentation is consistent across every target (aroma + infotainment).
# Mixing sanitized and non-sanitized objects in one binary is undefined
# behavior and a common source of "the sanitizer itself crashed" reports.
set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN)
    set(SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer -g -O1)
    set(SANITIZER_LINK_FLAGS -fsanitize=address)
    message(STATUS "Sanitizer: AddressSanitizer + LeakSanitizer ENABLED")
elseif(ENABLE_TSAN)
    set(SANITIZER_COMPILE_FLAGS -fsanitize=thread -fno-omit-frame-pointer -g -O1)
    set(SANITIZER_LINK_FLAGS -fsanitize=thread)
    message(STATUS "Sanitizer: ThreadSanitizer ENABLED")
else()
    message(STATUS "Sanitizer: DISABLED")
endif()

if(NOT TARGET aroma)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aroma_root)
endif()

target_compile_options(aroma PRIVATE 
    -g
    ${SANITIZER_COMPILE_FLAGS}
)
target_link_options(aroma PRIVATE
    ${SANITIZER_LINK_FLAGS}
)

# Find required packages for Bluetooth speaker
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
    ${SANITIZER_COMPILE_FLAGS}
)
target_link_options(infotainment PRIVATE
    ${SANITIZER_LINK_FLAGS}
)

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

# Include directories
target_include_directories(infotainment PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib"
)

# Add Bluetooth speaker include directories
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
        -g
        ${DBUS_CFLAGS_OTHER}
        ${PULSE_CFLAGS_OTHER}
    )
endif()

# Platform-specific linking
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
    
    # Disable Bluetooth speaker for web builds
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

# Add Bluetooth speaker libraries
if(ENABLE_BLUETOOTH_SPEAKER AND NOT EMSCRIPTEN)
    target_link_libraries(infotainment PRIVATE
        ${DBUS_LIBRARIES}
        ${PULSE_LIBRARIES}
    )
    message(STATUS "Bluetooth Speaker: ENABLED")
else()
    message(STATUS "Bluetooth Speaker: DISABLED")
endif()
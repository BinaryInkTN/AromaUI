cmake_minimum_required(VERSION 3.15)
project(settings_app C CXX)

enable_testing()

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

add_executable(infotainment
        core/main.c
        core/main_loop.c
        core/app_state.c
        core/app_registry.c
        pkg/package_manager.c
        apak/aroma_package.c
        apak/aroma_apak.c
        apak/aroma_incense.c
        apak/aroma_incense_loader.c
        setup/setup_store.c
        setup/setup_wizard.c
        apps/settings/settings_app.c
        voice/voice_handler.c
        core/theme_manager.c
        core/font_manager.c
        core/ui_animation_utils.c
        vehicle/vehicle_view.c
        vehicle/lock_screen.c
        vehicle/vehicle_camera.c
        vehicle/interior_controls.c
        vehicle/media_home.c
        vehicle/easter_egg.c
        vehicle/status_bar.c
        vehicle/tabs_manager.c
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
    target_sources(infotainment PRIVATE voice/voice_control.c)
    target_compile_definitions(infotainment PRIVATE AROMA_USE_VOICE_CONTROL)
    target_link_libraries(infotainment PRIVATE vosk)
    message(STATUS "Voice control: ENABLED")
else()
    message(STATUS "Voice control: DISABLED")
endif()

target_include_directories(infotainment PRIVATE
    # Public API first: bare "aroma_*.h" must resolve to include/,
    # not to the src/core/ forwarding wrappers of the same name.
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/vehicle"
    "${CMAKE_CURRENT_SOURCE_DIR}/setup"
    "${CMAKE_CURRENT_SOURCE_DIR}/voice"
    "${CMAKE_CURRENT_SOURCE_DIR}/pkg"
    "${CMAKE_CURRENT_SOURCE_DIR}/apak"
    # First-party BT service contracts (owned by the media/contacts
    # packages; the host only includes the contract headers).
    "${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/media/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/contacts/src"
)
# _GNU_SOURCE was previously pulled in by the Bluetooth block; keep it:
# several host sources rely on GNU extensions.
target_compile_definitions(infotainment PRIVATE _GNU_SOURCE)

set(AROMA_APAK_SOURCES
    apak/aroma_package.c
    apak/aroma_apak.c
    apak/aroma_incense.c
    apak/aroma_incense_loader.c
)
set_source_files_properties(${AROMA_APAK_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
)

find_package(ZLIB QUIET)
if(ZLIB_FOUND AND NOT EMSCRIPTEN)
    target_compile_definitions(infotainment PRIVATE AROMA_HAS_ZLIB=1)
    target_link_libraries(infotainment PRIVATE ZLIB::ZLIB)
endif()

if(UNIX AND NOT EMSCRIPTEN)
    target_link_libraries(infotainment PRIVATE ${CMAKE_DL_LIBS})
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
elseif(ENABLE_GLFW)
    target_link_libraries(infotainment PRIVATE
        aroma
        m
        pthread
        asound
        curl
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

if(UNIX AND NOT EMSCRIPTEN)
    # Native .apak plugins resolve aroma_* and host symbols from the host
    # process at dlopen time: export dynamic symbols, and pin the
    # plugin-facing API (plugin_api.syms) so LTO/GC cannot drop functions
    # only referenced by plugins.
    target_link_options(infotainment PRIVATE -rdynamic)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/plugin_api.syms" _plugin_api_syms)
    foreach(_sym IN LISTS _plugin_api_syms)
        if(_sym AND NOT _sym MATCHES "^#")
            target_link_options(infotainment PRIVATE "LINKER:--export-dynamic-symbol,${_sym}")
        endif()
    endforeach()
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
message(STATUS "GPU Hardware: ${ENABLE_GPU_HARDWARE}")
message(STATUS "Debug Symbols: ${ENABLE_DEBUG_SYMBOLS}")
message(STATUS "Sanitizers: ${ENABLE_ASAN} / ${ENABLE_TSAN}")
message(STATUS "LTO Enabled: ${CMAKE_BUILD_TYPE}==Release")
message(STATUS "=====================================")
message(STATUS "")

# --- First-party .apak packages ------------------------------------------------
# nav / media / contacts compile to native plugins, are packed with tools/apak.py
# and staged into <build>/packages/ (preinstalled system packages). Native
# plugins need dlopen, so this whole block is skipped for Emscripten (where the
# web build keeps settings/third-party UI packages only).
set(AROMA_PACKAGES_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(AROMA_APAK_DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/dist")
file(MAKE_DIRECTORY "${AROMA_PACKAGES_STAGING_DIR}" "${AROMA_APAK_DIST_DIR}")

if(NOT EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    # Adds build + pack rules for one first-party package:
    #   add_first_party_package(<leaf-id> <package-source-dir> <plugin-target> <stage-system>)
    # <stage-system> TRUE pre-installs into build/packages/ (system apps);
    # FALSE packs build/dist/<leaf>.apak only, for manual installation
    # (stale preinstalled copies are removed so rebuilds start clean).
    # Staging (plugin.so -> leaf dir) happens here, in the pack target
    # itself, and NOT as a POST_BUILD on the plugin: POST_BUILD only
    # fires when the plugin relinks, so an up-to-date plugin would
    # silently skip staging and the pack step would fail on the
    # missing plugin.so.
    # On ARM target builds the freshly packed (correct-arch) .apak is
    # additionally copied back into the source assets dir, so firmware
    # images and seed runs pick up matching-arch packages instead of
    # stale host-built ones. Host builds skip this to avoid churning
    # the tracked assets with wrong-arch binaries.
    function(add_first_party_package PKG_LEAF PKG_SRC_DIR PLUGIN_TARGET STAGE_SYSTEM)
        if(STAGE_SYSTEM)
            set(PKG_LEAF_DIR "${AROMA_PACKAGES_STAGING_DIR}/${PKG_LEAF}")
        else()
            set(PKG_LEAF_DIR "${CMAKE_CURRENT_BINARY_DIR}/apak-dist-stage/${PKG_LEAF}")
            file(REMOVE_RECURSE "${AROMA_PACKAGES_STAGING_DIR}/${PKG_LEAF}")
        endif()
        file(COPY "${PKG_SRC_DIR}/manifest.json" DESTINATION "${PKG_LEAF_DIR}")
        # Bundled assets travel with the package (preinstalled and .apak).
        if(EXISTS "${PKG_SRC_DIR}/assets")
            file(COPY "${PKG_SRC_DIR}/assets" DESTINATION "${PKG_LEAF_DIR}")
        endif()
        if(Python3_FOUND)
            add_custom_target(${PLUGIN_TARGET}_apak ALL
                DEPENDS ${PLUGIN_TARGET}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG_LEAF_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:${PLUGIN_TARGET}>"
                        "${PKG_LEAF_DIR}/plugin.so"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/apak.py"
                        pack "${PKG_LEAF_DIR}" -o "${AROMA_APAK_DIST_DIR}/${PKG_LEAF}.apak"
                COMMENT "Packing ${PKG_LEAF}.apak"
                VERBATIM
            )
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
                add_custom_command(TARGET ${PLUGIN_TARGET}_apak POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${AROMA_APAK_DIST_DIR}/${PKG_LEAF}.apak"
                            "${CMAKE_CURRENT_SOURCE_DIR}/assets/${PKG_LEAF}.apak"
                    COMMENT "Staging ${PKG_LEAF}.apak into source assets"
                    VERBATIM
                )
            endif()
        endif()
    endfunction()

    # Navigation (offline maps + routing).
    set(AROMA_PKG_LEAF_DIR "${AROMA_PACKAGES_STAGING_DIR}/com.aroma.nav")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/nav"
                     "${CMAKE_CURRENT_BINARY_DIR}/nav_plugin_build")
    unset(AROMA_PKG_LEAF_DIR)
    add_first_party_package(com.aroma.nav
        "${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/nav" nav_plugin TRUE)

    # Media (Bluetooth AVRCP now playing + devices).
    set(AROMA_PKG_LEAF_DIR "${AROMA_PACKAGES_STAGING_DIR}/com.aroma.media")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/media"
                     "${CMAKE_CURRENT_BINARY_DIR}/media_plugin_build")
    unset(AROMA_PKG_LEAF_DIR)
    add_first_party_package(com.aroma.media
        "${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/media" media_plugin TRUE)

    # Contacts (dialer + contact list + PBAP sync).
    set(AROMA_PKG_LEAF_DIR "${AROMA_PACKAGES_STAGING_DIR}/com.aroma.contacts")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/contacts"
                     "${CMAKE_CURRENT_BINARY_DIR}/contacts_plugin_build")
    unset(AROMA_PKG_LEAF_DIR)
    add_first_party_package(com.aroma.contacts
        "${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/contacts" contacts_plugin TRUE)

    # --- Games (host-chrome native plugins, third-party pattern) ------------
    set(AROMA_PKG_LEAF_DIR "${AROMA_PACKAGES_STAGING_DIR}/com.aroma.game.tictactoe")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/games/tictactoe"
                     "${CMAKE_CURRENT_BINARY_DIR}/tictactoe_plugin_build")
    unset(AROMA_PKG_LEAF_DIR)
    add_first_party_package(com.aroma.game.tictactoe
        "${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/games/tictactoe" tictactoe_plugin FALSE)

    set(AROMA_PKG_LEAF_DIR "${AROMA_PACKAGES_STAGING_DIR}/com.aroma.game.memory")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/games/memory"
                     "${CMAKE_CURRENT_BINARY_DIR}/memory_plugin_build")
    unset(AROMA_PKG_LEAF_DIR)
    add_first_party_package(com.aroma.game.memory
        "${CMAKE_CURRENT_SOURCE_DIR}/../package_examples/games/memory" memory_plugin FALSE)

    # The app seeds/loads these at runtime, so building `infotainment`
    # alone (e.g. `cmake --build . --target infotainment`, as packaging
    # builds do) must also (re)build the first-party .apaks with the SAME
    # toolchain/arch. Otherwise the app builds but ships/loads without
    # (or stale, wrong-arch) packages.
    foreach(_apak_target nav_plugin_apak media_plugin_apak contacts_plugin_apak
                         tictactoe_plugin_apak memory_plugin_apak)
        if(TARGET ${_apak_target})
            add_dependencies(infotainment ${_apak_target})
        endif()
    endforeach()

    # Deployment: `cmake --install --prefix /usr` (or DESTDIR for packaging)
    # places the freshly built ARM apaks straight into
    # /usr/share/infotainment/assets, where seeding discovers them.
    # A raw POST_BUILD cp to /usr/share is deliberately avoided: it would
    # need root during every build and contaminate cross-build hosts.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
        install(FILES
            "${AROMA_APAK_DIST_DIR}/com.aroma.nav.apak"
            "${AROMA_APAK_DIST_DIR}/com.aroma.media.apak"
            "${AROMA_APAK_DIST_DIR}/com.aroma.contacts.apak"
            "${AROMA_APAK_DIST_DIR}/com.aroma.game.tictactoe.apak"
            "${AROMA_APAK_DIST_DIR}/com.aroma.game.memory.apak"
            DESTINATION share/infotainment/assets)
    endif()
endif()

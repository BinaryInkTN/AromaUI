# Install script for directory: /home/yassine/Projects/AromaUI/vendors/litehtml

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/yassine/Android/Sdk/ndk/25.1.8937393/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xlibrariesx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/yassine/Projects/AromaUI/examples/14_video/android/app/.cxx/Debug/2z35731k/x86_64/aroma_lib/litehtml_build/liblitehtml.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/litehtml" TYPE FILE FILES
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/background.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/borders.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/codepoint.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_length.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_margins.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_offsets.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_position.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_selector.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_parser.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_tokenizer.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/document.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/document_container.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_anchor.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_base.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_before_after.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_body.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_break.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_cdata.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_comment.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_div.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_font.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_image.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_link.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_para.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_script.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_space.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_style.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_table.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_td.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_text.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_title.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/el_tr.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/element.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/encodings.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/html.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/html_tag.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/html_microsyntaxes.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/iterators.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/media_query.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/os_types.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/style.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/stylesheet.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/table.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/types.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/pixel_type.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/url.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/url_path.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/utf8_strings.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/web_color.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/num_cvt.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_properties.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/line_box.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_item.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_flex.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_image.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_inline.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_table.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_inline_context.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_block_context.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/render_block.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/master_css.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/string_id.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/formatting_context.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/flex_item.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/flex_line.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/gradient.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/font_description.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/scroll_view.h"
    "/home/yassine/Projects/AromaUI/vendors/litehtml/include/litehtml/css_values.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/litehtml" TYPE FILE FILES "/home/yassine/Projects/AromaUI/vendors/litehtml/cmake/litehtmlConfig.cmake")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/litehtml/litehtmlTargets.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/litehtml/litehtmlTargets.cmake"
         "/home/yassine/Projects/AromaUI/examples/14_video/android/app/.cxx/Debug/2z35731k/x86_64/aroma_lib/litehtml_build/CMakeFiles/Export/lib/cmake/litehtml/litehtmlTargets.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/litehtml/litehtmlTargets-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/litehtml/litehtmlTargets.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/litehtml" TYPE FILE FILES "/home/yassine/Projects/AromaUI/examples/14_video/android/app/.cxx/Debug/2z35731k/x86_64/aroma_lib/litehtml_build/CMakeFiles/Export/lib/cmake/litehtml/litehtmlTargets.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/litehtml" TYPE FILE FILES "/home/yassine/Projects/AromaUI/examples/14_video/android/app/.cxx/Debug/2z35731k/x86_64/aroma_lib/litehtml_build/CMakeFiles/Export/lib/cmake/litehtml/litehtmlTargets-debug.cmake")
  endif()
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/yassine/Projects/AromaUI/examples/14_video/android/app/.cxx/Debug/2z35731k/x86_64/aroma_lib/litehtml_build/src/gumbo/cmake_install.cmake")

endif()


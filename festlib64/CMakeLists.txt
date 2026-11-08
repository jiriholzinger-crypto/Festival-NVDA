# festlib.dll (64-bit) - Festival engine for the NVDA Festival synthDriver.
# See BUILD.md. Expects this layout:
#   <root>/speech_tools        (built with meson into speech_tools/build-win64)
#   <root>/festival            (Festival 2.5.1 sources with CMakeLists.txt)
#   <root>/festlib64           (this directory)
cmake_minimum_required(VERSION 3.16)
project(festlib CXX C)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(EST_DIR "${ROOT_DIR}/speech_tools" CACHE PATH "speech_tools source dir")
set(EST_LIB_DIR "${EST_DIR}/build-win64" CACHE PATH "dir with estools/estbase/eststring libraries")
set(FESTIVAL_DIR "${ROOT_DIR}/festival" CACHE PATH "festival source dir")

add_subdirectory(${FESTIVAL_DIR} festival-build)

# meson names static libs libX.a (MinGW and MSVC alike)
foreach(l estools estbase eststring)
  string(TOUPPER ${l} V)
  set(${V} "${EST_LIB_DIR}/lib${l}.a")
  if(NOT EXISTS "${${V}}")
    message(FATAL_ERROR "${${V}} not found - build speech_tools first (see BUILD.md)")
  endif()
endforeach()

add_library(festlib SHARED
  src/festlib.cpp
  src/player.cpp
  third_party/tinyxml2/tinyxml2.cpp
  src/festlib.rc)
set_target_properties(festlib PROPERTIES PREFIX "" OUTPUT_NAME festlib)
set_property(TARGET festlib PROPERTY CXX_STANDARD 17)
target_include_directories(festlib PRIVATE src third_party/tinyxml2)
target_compile_definitions(festlib PRIVATE UNICODE _UNICODE SYSTEM_IS_WIN32 NOMINMAX)
# Festival must come before the speech tools libraries (static link order)
target_link_libraries(festlib PRIVATE Festival ${ESTOOLS} ${ESTBASE} ${ESTSTRING} winmm ws2_32 wsock32 ole32)

if(MSVC)
  target_compile_options(festlib PRIVATE /EHa /W3)
  set_property(TARGET festlib PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
  set_property(TARGET Festival PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
  target_compile_options(festlib PRIVATE -Wall -Wno-unused-parameter)
  # self-contained DLL: no libc++/libunwind/winpthread DLLs needed next to it
  target_link_options(festlib PRIVATE -static -Wl,--kill-at -Wl,--no-insert-timestamp -s)
endif()

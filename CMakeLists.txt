cmake_minimum_required(VERSION 3.21)
project(cmsis-dap-blaster VERSION 0.1.0 LANGUAGES C)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Only Windows and Linux x86_64 are supported.")
endif()

include(CheckCSourceCompiles)
check_c_source_compiles("
    #if !defined(__x86_64__) && !defined(_M_X64)
    #error Unsupported architecture
    #endif
    typedef char require_64_bit_pointers[sizeof(void *) == 8 ? 1 : -1];
    int main(void) { return 0; }
" TARGET_X86_64)
if(NOT TARGET_X86_64)
    message(FATAL_ERROR "An x86_64 compiler target is required. Use -A x64 with Visual Studio.")
endif()

option(BUILD_PLUGIN "Build the Quartus JTAG hardware plugin" ON)
option(BUILD_SCAN_TOOL "Build the standalone JTAG scan tool" ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(cmsis_dap_target_defaults target)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(CMSIS_DAP_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/export" CACHE PATH
    "Output directory for plugin and scan tool binaries")

if(BUILD_PLUGIN OR BUILD_SCAN_TOOL)
    set(CMSIS_DAP_ROOT "" CACHE PATH "Optional hidapi and libusb dependency directory")
    find_path(HIDAPI_INCLUDE_DIR NAMES hidapi.h
        HINTS "${CMSIS_DAP_ROOT}"
        PATH_SUFFIXES include/hidapi hidapi include)
    find_library(HIDAPI_LIBRARY NAMES hidapi-hidraw hidapi-libusb hidapi
        HINTS "${CMSIS_DAP_ROOT}" PATH_SUFFIXES lib lib64)
    find_path(LIBUSB_INCLUDE_DIR NAMES libusb.h
        HINTS "${CMSIS_DAP_ROOT}"
        PATH_SUFFIXES include/libusb-1.0 libusb-1.0 include)
    find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0
        HINTS "${CMSIS_DAP_ROOT}" PATH_SUFFIXES lib lib64)
    if(NOT HIDAPI_INCLUDE_DIR OR NOT HIDAPI_LIBRARY OR NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
        message(FATAL_ERROR
            "hidapi or libusb headers and library not found. Install development packages, "
            "set CMSIS_DAP_ROOT to their installation prefix, or set HIDAPI_INCLUDE_DIR, "
            "HIDAPI_LIBRARY, LIBUSB_INCLUDE_DIR and LIBUSB_LIBRARY.")
    endif()

    add_library(HIDAPI::hidapi UNKNOWN IMPORTED)
    set_target_properties(HIDAPI::hidapi PROPERTIES
        IMPORTED_LOCATION "${HIDAPI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HIDAPI_INCLUDE_DIR}"
    )
    add_library(LIBUSB::libusb UNKNOWN IMPORTED)
    set_target_properties(LIBUSB::libusb PROPERTIES
        IMPORTED_LOCATION "${LIBUSB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}"
    )
    if(WIN32)
        target_link_libraries(HIDAPI::hidapi INTERFACE setupapi hid)
        target_link_libraries(LIBUSB::libusb INTERFACE setupapi advapi32 cfgmgr32)
    else()
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        target_link_libraries(HIDAPI::hidapi INTERFACE Threads::Threads)
    endif()

endif()

set(cmsis_dap_sources
    src/cmsis_dap_transport.c
    src/cmsis_dap_device.c
    src/cmsis_dap_clock.c
    src/cmsis_dap_autotune.c
    src/cmsis_dap_jtag.c
    src/quartus_plugin.c
)

if(BUILD_PLUGIN OR BUILD_SCAN_TOOL)
    add_library(cmsis_dap_core OBJECT ${cmsis_dap_sources})
    cmsis_dap_target_defaults(cmsis_dap_core)
    set_target_properties(cmsis_dap_core PROPERTIES POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)
    target_link_libraries(cmsis_dap_core PRIVATE HIDAPI::hidapi LIBUSB::libusb)

    set(blaster_targets)
    if(BUILD_PLUGIN)
        add_library(jtag_hw_cmsis_dap SHARED)
        list(APPEND blaster_targets jtag_hw_cmsis_dap)
        if(NOT WIN32)
            target_link_options(jtag_hw_cmsis_dap PRIVATE
                "LINKER:--exclude-libs,ALL" "LINKER:-z,defs")
        endif()
    endif()
    if(BUILD_SCAN_TOOL)
        add_executable(cmsis_dap_scan tool/cmsis_dap_scan.c)
        list(APPEND blaster_targets cmsis_dap_scan)
    endif()
    foreach(target IN LISTS blaster_targets)
        cmsis_dap_target_defaults(${target})
        target_link_libraries(${target} PRIVATE cmsis_dap_core HIDAPI::hidapi LIBUSB::libusb)
        foreach(kind LIBRARY RUNTIME ARCHIVE)
            set_target_properties(${target} PROPERTIES
                ${kind}_OUTPUT_DIRECTORY "$<1:${CMSIS_DAP_OUTPUT_DIRECTORY}>")
        endforeach()
    endforeach()
endif()

include(GNUInstallDirs)
if(BUILD_PLUGIN)
    install(TARGETS jtag_hw_cmsis_dap
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(BUILD_SCAN_TOOL)
    install(TARGETS cmsis_dap_scan RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(BUILD_PLUGIN AND NOT WIN32)
    install(FILES 60-cmsis-dap-blaster.rules
        DESTINATION ${CMAKE_INSTALL_DATADIR}/cmsis-dap-blaster)
endif()

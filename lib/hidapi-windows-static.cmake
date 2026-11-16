# hidapi 0.14.0 also includes the descriptor implementation from hid.c unless
# this guard is defined. CMake already compiles that implementation separately.
# HID_API_NO_EXPORT_DEFINE on the static target still disables DLL exports.
if(WIN32 AND NOT BUILD_SHARED_LIBS)
    add_compile_definitions(hidapi_winapi_EXPORTS)
endif()

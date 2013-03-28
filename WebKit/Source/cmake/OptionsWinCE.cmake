include(OptionsWindows)

add_definitions(-D_CE_CRT_ALLOW_WIN_MINMAX)
add_definitions(-DWTF_USE_WCHAR_UNICODE=1)
add_definitions(-DWTF_USE_WININET=1)
add_definitions(-DWTF_CPU_ARM_TRADITIONAL -DWINCEBASIC)
add_definitions(-DJS_NO_EXPORT)
add_definitions(-DHAVE_ACCESSIBILITY=0)
add_definitions(-DJSCCOLLECTOR_VIRTUALMEM_RESERVATION=0x200000)

if (NOT 3RDPARTY_DIR)
    if (EXISTS $ENV{WEBKITTHIRDPARTYDIR})
        set(3RDPARTY_DIR $ENV{WEBKITTHIRDPARTYDIR})
    else ()
        message(FATAL_ERROR "You must provide a third party directory for WinCE port.")
    endif ()
endif ()

include_directories(${3RDPARTY_DIR}/ce-compat)
add_subdirectory(${3RDPARTY_DIR} "${CMAKE_CURRENT_BINARY_DIR}/3rdparty")

WEBKIT_OPTION_BEGIN()
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_DRAG_SUPPORT OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_FTPDIR OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_INSPECTOR OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_SYSTEM_MALLOC ON)
WEBKIT_OPTION_END()

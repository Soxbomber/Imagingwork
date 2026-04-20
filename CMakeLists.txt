cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(Imagingwork)

if(MSVC)
    add_compile_options(/utf-8)
    # Force /MD (Release CRT) in all configurations so Debug builds can link
    # against GenICam SDK, which ships only /MD DLLs (no /MDd variants).
    # This loses the debug-heap overrun checks but keeps full debugger support.
    foreach(_flag
        CMAKE_C_FLAGS_DEBUG   CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/MDd" "/MD" ${_flag} "${${_flag}}")
    endforeach()
    # Since we use /MD everywhere, map Qt's Debug-config imports to their
    # Release builds (Qt5Core.dll, not Qt5Cored.dll) to avoid CRT mismatch.
    set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
endif()

set(CMAKE_GENERATOR_PLATFORM "x64"  CACHE STRING "" FORCE)
set(CMAKE_GENERATOR_TOOLSET  "v143" CACHE STRING "" FORCE)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network)

add_subdirectory(device)
add_subdirectory(viewer)
add_subdirectory(camera)
add_subdirectory(camera/uvc)
add_subdirectory(camera/gige)
add_subdirectory(app)

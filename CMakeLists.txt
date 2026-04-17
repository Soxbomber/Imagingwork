cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(Imagingwork)

if(MSVC)
    add_compile_options(/utf-8)
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

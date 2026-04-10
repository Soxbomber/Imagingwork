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

# Qt5 경로 지정:
#   cmake .. -DQt5_DIR="C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5"
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

# 서브모듈 등록 순서: 의존성 낮은 순
#   device -> viewer -> camera -> app
add_subdirectory(device)
add_subdirectory(viewer)
add_subdirectory(camera)
add_subdirectory(camera/uvc)
add_subdirectory(app)

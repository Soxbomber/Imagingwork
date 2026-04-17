add_library(device STATIC
    deviceinfo.h
    devicemanager.h
    devicemanager.cpp
    deviceitemwidget.h
    deviceitemwidget.cpp
)
target_include_directories(device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(device PUBLIC Qt5::Core Qt5::Widgets)
if(MSVC)
    target_compile_options(device PRIVATE /utf-8)
endif()

# 출력 경로 명시 (LNK1104 방지)
set_target_properties(device PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

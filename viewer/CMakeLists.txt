add_library(viewer STATIC
    ImageViewerDock.h
    ImageViewerDock.cpp
)
target_include_directories(viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(viewer PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets device)
if(MSVC)
    target_compile_options(viewer PRIVATE /utf-8)
endif()

# 출력 경로 명시 (LNK1104 방지)
set_target_properties(viewer PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# app module - main window, UI, entry point

set(APP_SOURCES
    main.cpp
    Imagingwork.h
    Imagingwork.cpp
    ui_Imagingwork.h
    ui_Imagingwork.cpp
    Camera_submenu.h
    Camera_submenu.cpp
    Iconlabel.h
    Iconlabel.cpp
)

# Qt 리소스 (icons + CTI 포함)
set(APP_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources.qrc
)

add_executable(${PROJECT_NAME} WIN32
    ${APP_SOURCES}
    ${APP_RESOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    camera
    device
    viewer
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        UNICODE
        _UNICODE
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        /MP
        /permissive-
        /Zc:__cplusplus
    )
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# windeployqt: Qt DLL 자동 배포
find_program(WINDEPLOYQT windeployqt
    HINTS "${Qt5_DIR}/../../../bin"
)
if(WINDEPLOYQT)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${WINDEPLOYQT}"
            --no-compiler-runtime
            --no-translations
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Running windeployqt..."
    )
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

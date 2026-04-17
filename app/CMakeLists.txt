set(APP_SOURCES
    main.cpp
    Imagingwork.h  Imagingwork.cpp
    ui_Imagingwork.h  ui_Imagingwork.cpp
    Camera_submenu.h  Camera_submenu.cpp
    Iconlabel.h  Iconlabel.cpp
)

add_executable(${PROJECT_NAME} WIN32
    ${APP_SOURCES}
    ${CMAKE_SOURCE_DIR}/resources.qrc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/camera
    ${CMAKE_SOURCE_DIR}/camera/genicam
    ${CMAKE_SOURCE_DIR}/camera/uvc
    ${CMAKE_SOURCE_DIR}/camera/gige
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    camera uvc gige device viewer
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Multimedia Qt5::Network
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W3 /MP)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# windeployqt
find_program(WINDEPLOYQT windeployqt HINTS "${Qt5_DIR}/../../../bin")
if(WINDEPLOYQT)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${WINDEPLOYQT}"
            --no-compiler-runtime --no-translations
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "windeployqt...")
endif()

# INSTALL
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install"
        CACHE PATH "Install prefix" FORCE)
endif()
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

# vcpkg DLL
foreach(_dir
    "$ENV{VCPKG_ROOT}/installed/x64-windows/bin"
    "C:/vcpkg/installed/x64-windows/bin")
    if(EXISTS "${_dir}")
        install(DIRECTORY "${_dir}/" DESTINATION bin
            FILES_MATCHING PATTERN "libusb*.dll" PATTERN "zlib*.dll")
        break()
    endif()
endforeach()

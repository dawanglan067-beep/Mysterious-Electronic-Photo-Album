cmake_minimum_required(VERSION 3.19)
project(Album LANGUAGES CXX)

# Preserve cl.exe's raw /showIncludes encoding for Ninja. CMake's decoded
# localized prefix can otherwise drop header dependencies and leave objects
# with incompatible class layouts after a header changes.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(_album_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/album-includes")
    file(MAKE_DIRECTORY "${_album_probe_dir}")
    file(WRITE "${_album_probe_dir}/probe.h" "// Header dependency probe.\n")
    file(WRITE "${_album_probe_dir}/probe.cpp" "#include \"probe.h\"\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /Zs
            "${_album_probe_dir}/probe.cpp"
        OUTPUT_VARIABLE _album_includes
        ERROR_VARIABLE _album_includes_error
        RESULT_VARIABLE _album_probe_result
        ENCODING NONE
    )
    string(REGEX MATCH "(^|\n)([^:\r\n]+: [^:\r\n]+: +)"
        _album_prefix_match "${_album_includes}\n${_album_includes_error}")
    if(NOT _album_probe_result EQUAL 0 OR NOT _album_prefix_match)
        message(FATAL_ERROR "Cannot detect the MSVC header dependency prefix.")
    endif()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
endif()

set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia Svg)


qt_standard_project_setup()

qt_add_executable(Album
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    rc.qrc
    app.rc
    wizard.h wizard.cpp wizard.ui
    prosetpage.h prosetpage.cpp prosetpage.ui
    confirmpage.h confirmpage.cpp confirmpage.ui
    protree.h protree.cpp protree.ui
    protreewidget.h protreewidget.cpp
    protreeitem.h protreeitem.cpp
    const.h
    protreethread.h protreethread.cpp
    removeprodialog.h removeprodialog.cpp removeprodialog.ui
    opentreethread.h opentreethread.cpp
    picshow.h picshow.cpp picshow.ui
    picbtn.h picbtn.cpp
    slideshowdlg.h slideshowdlg.cpp slideshowdlg.ui
    picstatebtn.h picstatebtn.cpp
    picanimationwid.h picanimationwid.cpp
    prolistwid.h prolistwid.cpp
    prelistitem.h prelistitem.cpp
)

target_link_libraries(Album
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Multimedia
        Qt::Svg
        winmm
)

target_include_directories(Album
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

include(GNUInstallDirs)

install(TARGETS Album
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Album
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

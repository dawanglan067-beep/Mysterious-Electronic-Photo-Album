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
set_property(SOURCE app.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/icon/app/album.ico")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia Svg)


qt_standard_project_setup()

qt_add_executable(Album
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    albumfolderpicker.h albumfolderpicker.cpp
    histogramwidget.h histogramwidget.cpp
    imageprocessor.h imageprocessor.cpp
    imageexportservice.h imageexportservice.cpp
    musiccontroller.h musiccontroller.cpp
    musicsettingsdialog.h musicsettingsdialog.cpp
    audiofileprobe.h audiofileprobe.cpp
    workspacesettings.h workspacesettings.cpp
    duplicatephotofinder.h duplicatephotofinder.cpp
    photoentry.h
    photowallmodel.h photowallmodel.cpp
    photowallwidget.h photowallwidget.cpp
    thumbnailloader.h thumbnailloader.cpp
    albumexportdialog.h albumexportdialog.cpp
    albumpdfexporter.h albumpdfexporter.cpp
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
    photoeditdocument.h photoeditdocument.cpp
    cropdialog.h cropdialog.cpp
)

# Some Qt Creator/MSVC environments emit /showIncludes in an encoding that
# Ninja does not recognize. Keep project headers as explicit dependencies too:
# a stale main.cpp can otherwise allocate an older, smaller MainWindow on the
# stack while its new constructor writes beyond that allocation.
file(GLOB _album_project_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB _album_project_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set_property(SOURCE ${_album_project_sources} APPEND PROPERTY
    OBJECT_DEPENDS "${_album_project_headers}")

target_link_libraries(Album
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Multimedia
        Qt::Svg
        winmm
        ole32 shell32 uuid
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

option(ALBUM_BUILD_FEATURE_TESTS "Build photo wall and PDF regression tests" OFF)
if(ALBUM_BUILD_FEATURE_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(AlbumNativePickerTests tests/nativefolderpickertest.cpp albumfolderpicker.cpp albumfolderpicker.h)
    target_include_directories(AlbumNativePickerTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(AlbumNativePickerTests PRIVATE Qt::Widgets ole32 oleaut32 shell32 uuid)
    get_target_property(_album_test_sources Album SOURCES)
    list(REMOVE_ITEM _album_test_sources main.cpp protreewidget.cpp)
    list(FILTER _album_test_sources EXCLUDE REGEX "\\$<")
    # Tests must never read/write the user's real album or music preferences.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/protreewidget.cpp" _album_test_tree)
    string(REPLACE "AlbumStudio" "AlbumFeatureTests" _album_test_tree "${_album_test_tree}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/feature_test_tree.cpp" "${_album_test_tree}")
    qt_add_executable(AlbumFeatureTests
        tests/albumfeaturetests.cpp
        ${_album_test_sources}
        "${CMAKE_CURRENT_BINARY_DIR}/feature_test_tree.cpp"
    )
    set_target_properties(AlbumFeatureTests PROPERTIES AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(AlbumFeatureTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(AlbumFeatureTests PRIVATE Qt::Widgets Qt::Svg Qt::Multimedia Qt::Test winmm ole32 shell32 uuid)
    add_test(NAME AlbumFeatures COMMAND AlbumFeatureTests)
    get_filename_component(_album_qt_root "${Qt6_DIR}/../../.." ABSOLUTE)
    set_tests_properties(AlbumFeatures PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_album_qt_root}/plugins;QT_FORCE_STDERR_LOGGING=1")
endif()

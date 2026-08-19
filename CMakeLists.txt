cmake_minimum_required(VERSION 3.19)
project(Album LANGUAGES CXX)

set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

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
    comfirmpage.h comfirmpage.cpp comfirmpage.ui
    protree.h protree.cpp protree.ui
    protreewidget.h protreewidget.cpp
    protreeitem.h protreeitem.cpp
    const.h
)

target_link_libraries(Album
    PRIVATE
        Qt::Core
        Qt::Widgets
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

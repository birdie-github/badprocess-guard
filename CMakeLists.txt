cmake_minimum_required(VERSION 3.16)

project(badprocess-guard VERSION 0.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BADPROCESS_GUARD_QT6 "Build with Qt6 instead of Qt5" OFF)

if(BADPROCESS_GUARD_QT6)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_PACKAGE Qt5)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

add_executable(badprocess-guard
    src/main.cpp
    src/configuration.cpp
    src/configuration.h
    src/processmonitor.cpp
    src/processmonitor.h
    src/processentrywidget.cpp
    src/processentrywidget.h
    src/alertwindow.cpp
    src/alertwindow.h
    src/settingsdialog.cpp
    src/settingsdialog.h
)

target_link_libraries(badprocess-guard PRIVATE ${QT_PACKAGE}::Widgets)

if(NOT WIN32)
find_package(X11 QUIET)
if(X11_FOUND)
    target_link_libraries(badprocess-guard PRIVATE X11::X11)
    target_compile_definitions(badprocess-guard PRIVATE BADPROCESS_GUARD_HAVE_X11=1)
endif()
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/process-mapping.ini
    ${CMAKE_CURRENT_BINARY_DIR}/process-mapping.ini
    COPYONLY
)

target_compile_options(badprocess-guard PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS badprocess-guard RUNTIME DESTINATION bin)
install(FILES process-mapping.ini DESTINATION share/badprocess-guard)

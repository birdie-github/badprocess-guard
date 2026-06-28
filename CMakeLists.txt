cmake_minimum_required(VERSION 3.16)

project(badprocess-guard VERSION 0.1.0 LANGUAGES CXX)

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

target_compile_options(badprocess-guard PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS badprocess-guard RUNTIME DESTINATION bin)

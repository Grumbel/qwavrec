# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

# Source of truth: top-level VERSION (e.g. "0.1.0-dev"), or
# -DPROJECT_VERSION_FULL=... from packaging (Nix may append .<revCount>+g<rev>).
if(NOT DEFINED PROJECT_VERSION_FULL)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_FULL LIMIT_COUNT 1)
endif()

# project() needs a numeric version; strip -dev / +git / similar suffixes.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" _project_version_cmake "${PROJECT_VERSION_FULL}")
if(NOT _project_version_cmake)
  set(_project_version_cmake "0.0.0")
endif()

project(qwavrec VERSION ${_project_version_cmake} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED libpulse libpulse-simple)

qt_add_executable(qwavrec
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/levelmeter.cpp
    src/levelmeter.h
    src/waveformwidget.cpp
    src/waveformwidget.h
    src/wavwriter.cpp
    src/wavwriter.h
    src/wavfile.cpp
    src/wavfile.h
    src/recordinghistory.cpp
    src/recordinghistory.h
    src/markedslider.cpp
    src/markedslider.h
    src/historydialog.cpp
    src/historydialog.h
    src/pulsebackend.cpp
    src/pulsebackend.h
)

target_include_directories(qwavrec PRIVATE ${PULSE_INCLUDE_DIRS})
target_link_libraries(qwavrec PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    ${PULSE_LIBRARIES}
)
target_compile_options(qwavrec PRIVATE ${PULSE_CFLAGS_OTHER})
target_compile_definitions(qwavrec PRIVATE
    QWAVREC_VERSION="${PROJECT_VERSION_FULL}"
)

install(TARGETS qwavrec DESTINATION bin)
install(FILES data/qwavrec.desktop DESTINATION share/applications)
install(FILES data/icons/qwavrec.svg
        DESTINATION share/icons/hicolor/scalable/apps)

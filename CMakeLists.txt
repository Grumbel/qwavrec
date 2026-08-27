# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(qwavrec VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

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
    src/wavplayer.cpp
    src/wavplayer.h
    src/recordinghistory.cpp
    src/recordinghistory.h
)

target_link_libraries(qwavrec PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
)

install(TARGETS qwavrec DESTINATION bin)
install(FILES data/qwavrec.desktop DESTINATION share/applications)
install(FILES data/icons/qwavrec.svg
        DESTINATION share/icons/hicolor/scalable/apps)

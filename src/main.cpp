// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

#ifndef QWAVREC_VERSION
#  define QWAVREC_VERSION "0.0.0"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QWavRec"));
    QApplication::setApplicationVersion(QStringLiteral(QWAVREC_VERSION));
    QApplication::setOrganizationName(QStringLiteral("QWavRec"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Simple WAV recorder/player (PulseAudio sources and sinks)."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    MainWindow window;
    window.show();

    return app.exec();
}

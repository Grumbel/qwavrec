// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVWRITER_H
#define WAVWRITER_H

#include <QAudioFormat>
#include <QFile>
#include <QString>

/**
 * Minimal PCM WAV writer. Header is finalized in close().
 */
class WavWriter
{
public:
    bool open(const QString &path, const QAudioFormat &format);
    void write(const char *data, qint64 size);
    bool close(); // rewrite header with final sizes
    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_path; }
    qint64 dataBytes() const { return m_dataBytes; }

private:
    void writeHeader(quint32 dataSize);

    QFile m_file;
    QString m_path;
    QAudioFormat m_format;
    qint64 m_dataBytes = 0;
};

#endif

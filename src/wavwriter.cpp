// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wavwriter.h"

#include <QDataStream>
#include <QtEndian>

bool WavWriter::open(const QString &path, const QAudioFormat &format)
{
    close();
    m_path = path;
    m_format = format;
    m_dataBytes = 0;

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    // Placeholder header (44 bytes); finalized in close()
    writeHeader(0);
    return true;
}

void WavWriter::write(const char *data, qint64 size)
{
    if (!m_file.isOpen() || size <= 0)
        return;
    m_file.write(data, size);
    m_dataBytes += size;
}

bool WavWriter::close()
{
    if (!m_file.isOpen())
        return true;
    writeHeader(static_cast<quint32>(m_dataBytes));
    m_file.close();
    return true;
}

void WavWriter::writeHeader(quint32 dataSize)
{
    const int sampleRate = m_format.sampleRate();
    const int channels = m_format.channelCount();
    int bitsPerSample = 16;
    switch (m_format.sampleFormat()) {
    case QAudioFormat::UInt8: bitsPerSample = 8; break;
    case QAudioFormat::Int16: bitsPerSample = 16; break;
    case QAudioFormat::Int32: bitsPerSample = 32; break;
    case QAudioFormat::Float: bitsPerSample = 32; break;
    default: bitsPerSample = 16; break;
    }
    const quint32 byteRate = quint32(sampleRate * channels * bitsPerSample / 8);
    const quint16 blockAlign = quint16(channels * bitsPerSample / 8);
    const quint32 riffSize = 36 + dataSize;

    m_file.seek(0);
    QDataStream out(&m_file);
    out.setByteOrder(QDataStream::LittleEndian);

    out.writeRawData("RIFF", 4);
    out << riffSize;
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32(16); // PCM fmt chunk size
    // Float uses WAVE_FORMAT_IEEE_FLOAT (3); integer PCM is 1
    const quint16 audioFormat = (m_format.sampleFormat() == QAudioFormat::Float) ? 3 : 1;
    out << audioFormat;
    out << quint16(channels);
    out << quint32(sampleRate);
    out << byteRate;
    out << blockAlign;
    out << quint16(bitsPerSample);
    out.writeRawData("data", 4);
    out << dataSize;
}

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wavfile.h"

#include <QFile>
#include <QtEndian>
#include <QtMath>

namespace WavFile {

Info load(const QString &path)
{
    Info info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("Could not open file");
        return info;
    }

    const QByteArray riff = file.read(12);
    if (riff.size() < 12 || !riff.startsWith("RIFF") || riff.mid(8, 4) != "WAVE") {
        info.error = QStringLiteral("Not a RIFF/WAVE file");
        return info;
    }

    bool gotFmt = false;
    qint64 dataOffset = -1;
    quint32 dataSize = 0;

    while (!file.atEnd()) {
        const QByteArray id = file.read(4);
        const QByteArray szb = file.read(4);
        if (id.size() < 4 || szb.size() < 4)
            break;
        const quint32 sz = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(szb.constData()));
        const qint64 chunkPos = file.pos();

        if (id == "fmt ") {
            QByteArray fmt = file.read(qMin(sz, quint32(64)));
            if (fmt.size() < 16) {
                info.error = QStringLiteral("Invalid fmt chunk");
                return info;
            }
            const quint16 audioFormat = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmt.constData()));
            const quint16 channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmt.constData() + 2));
            const quint32 sampleRate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(fmt.constData() + 4));
            const quint16 bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmt.constData() + 14));

            info.format.setChannelCount(channels);
            info.format.setSampleRate(int(sampleRate));
            if (audioFormat == 1) {
                if (bits == 8) info.format.setSampleFormat(QAudioFormat::UInt8);
                else if (bits == 16) info.format.setSampleFormat(QAudioFormat::Int16);
                else if (bits == 32) info.format.setSampleFormat(QAudioFormat::Int32);
                else {
                    info.error = QStringLiteral("Unsupported PCM bit depth");
                    return info;
                }
            } else if (audioFormat == 3 && bits == 32) {
                info.format.setSampleFormat(QAudioFormat::Float);
            } else {
                info.error = QStringLiteral("Unsupported WAV format");
                return info;
            }
            gotFmt = true;
            if (int(sz) > fmt.size())
                file.seek(chunkPos + sz + (sz & 1));
        } else if (id == "data") {
            dataOffset = chunkPos;
            dataSize = sz;
            break;
        } else {
            file.seek(chunkPos + sz + (sz & 1));
        }
    }

    if (!gotFmt) {
        info.error = QStringLiteral("WAV missing fmt chunk");
        return info;
    }
    if (dataOffset < 0) {
        info.error = QStringLiteral("WAV missing data chunk");
        return info;
    }

    file.seek(dataOffset);
    info.pcm = dataSize > 0 ? file.read(dataSize) : QByteArray();
    if (dataSize > 0 && info.pcm.isEmpty()) {
        info.error = QStringLiteral("Could not read WAV data");
        return info;
    }
    info.ok = true;
    return info;
}

QVector<float> peaks(const QByteArray &pcm, const QAudioFormat &format, int targetBins)
{
    QVector<float> out;
    const int bpf = format.bytesPerFrame();
    if (bpf <= 0 || pcm.isEmpty() || targetBins <= 0)
        return out;

    const int frames = pcm.size() / bpf;
    out.reserve(targetBins);
    const double step = double(qMax(1, frames)) / targetBins;

    for (int i = 0; i < targetBins; ++i) {
        const int a = int(i * step);
        const int b = qMin(int((i + 1) * step), frames);
        float mx = 0.f;
        for (int f = a; f < b; ++f) {
            const char *frame = pcm.constData() + f * bpf;
            float sample = 0.f;
            switch (format.sampleFormat()) {
            case QAudioFormat::Int16:
                sample = qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(frame)) / 32768.f;
                break;
            case QAudioFormat::Float:
                sample = *reinterpret_cast<const float *>(frame);
                break;
            case QAudioFormat::UInt8:
                sample = (quint8(frame[0]) - 128) / 128.f;
                break;
            case QAudioFormat::Int32:
                sample = qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(frame)) / 2147483648.f;
                break;
            default:
                break;
            }
            mx = qMax(mx, qAbs(sample));
        }
        out.append(mx);
    }
    return out;
}

bool peakNormalizeInt16(QByteArray &pcm)
{
    if (pcm.size() < 2)
        return false;
    auto *s = reinterpret_cast<qint16 *>(pcm.data());
    const int n = pcm.size() / 2;
    int peak = 0;
    for (int i = 0; i < n; ++i)
        peak = qMax(peak, qAbs(int(s[i])));
    if (peak <= 0)
        return true;
    if (peak >= 32767)
        return true;
    const double gain = 32767.0 / peak;
    for (int i = 0; i < n; ++i) {
        const int v = int(qRound(s[i] * gain));
        s[i] = qint16(qBound(-32768, v, 32767));
    }
    return true;
}

} // namespace WavFile

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wavfile.h"

#include <QFile>
#include <QtEndian>
#include <QtMath>
#include <QImage>
#include <QVector>
#include <cmath>
#include <functional>
#include <tuple>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

    const int channels = qMax(1, format.channelCount());
    for (int i = 0; i < targetBins; ++i) {
        const int a = int(i * step);
        const int b = qMin(int((i + 1) * step), frames);
        float mx = 0.f;
        for (int f = a; f < b; ++f) {
            const char *frame = pcm.constData() + f * bpf;
            for (int c = 0; c < channels; ++c) {
                float sample = 0.f;
                switch (format.sampleFormat()) {
                case QAudioFormat::Int16:
                    sample = qFromLittleEndian<qint16>(
                        reinterpret_cast<const uchar *>(frame) + c * 2) / 32768.f;
                    break;
                case QAudioFormat::Float:
                    sample = reinterpret_cast<const float *>(frame)[c];
                    break;
                case QAudioFormat::UInt8:
                    sample = (quint8(frame[c]) - 128) / 128.f;
                    break;
                case QAudioFormat::Int32:
                    sample = qFromLittleEndian<qint32>(
                        reinterpret_cast<const uchar *>(frame) + c * 4) / 2147483648.f;
                    break;
                default:
                    break;
                }
                mx = qMax(mx, qAbs(sample));
            }
        }
        out.append(mx);
    }
    return out;
}

ChannelPeaks channelPeaks(const QByteArray &pcm, const QAudioFormat &format, int targetBins)
{
    ChannelPeaks out;
    const int bpf = format.bytesPerFrame();
    if (bpf <= 0 || pcm.isEmpty() || targetBins <= 0)
        return out;

    const int frames = pcm.size() / bpf;
    const int channels = qMax(1, format.channelCount());
    out.left.reserve(targetBins);
    if (channels >= 2)
        out.right.reserve(targetBins);
    const double step = double(qMax(1, frames)) / targetBins;

    for (int i = 0; i < targetBins; ++i) {
        const int a = int(i * step);
        const int b = qMin(int((i + 1) * step), frames);
        float mxL = 0.f;
        float mxR = 0.f;
        for (int f = a; f < b; ++f) {
            const char *frame = pcm.constData() + f * bpf;
            for (int c = 0; c < channels; ++c) {
                float sample = 0.f;
                switch (format.sampleFormat()) {
                case QAudioFormat::Int16:
                    sample = qFromLittleEndian<qint16>(
                        reinterpret_cast<const uchar *>(frame) + c * 2) / 32768.f;
                    break;
                case QAudioFormat::Float:
                    sample = reinterpret_cast<const float *>(frame)[c];
                    break;
                case QAudioFormat::UInt8:
                    sample = (quint8(frame[c]) - 128) / 128.f;
                    break;
                case QAudioFormat::Int32:
                    sample = qFromLittleEndian<qint32>(
                        reinterpret_cast<const uchar *>(frame) + c * 4) / 2147483648.f;
                    break;
                default:
                    break;
                }
                const float aabs = qAbs(sample);
                if (c == 0)
                    mxL = qMax(mxL, aabs);
                else
                    mxR = qMax(mxR, aabs);
            }
        }
        out.left.append(mxL);
        if (channels >= 2)
            out.right.append(mxR);
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

namespace {

/** Peak sample across all channels in a frame (signed value of the louder channel). */
float sampleAt(const QByteArray &pcm, const QAudioFormat &format, int frame)
{
    const int bpf = format.bytesPerFrame();
    if (bpf <= 0 || frame < 0)
        return 0.f;
    const int frames = pcm.size() / bpf;
    if (frame >= frames)
        return 0.f;
    const int channels = qMax(1, format.channelCount());
    const char *p = pcm.constData() + frame * bpf;
    float best = 0.f;
    float bestAbs = 0.f;
    for (int c = 0; c < channels; ++c) {
        float sample = 0.f;
        switch (format.sampleFormat()) {
        case QAudioFormat::Int16:
            sample = qFromLittleEndian<qint16>(
                reinterpret_cast<const uchar *>(p) + c * 2) / 32768.f;
            break;
        case QAudioFormat::Float:
            sample = reinterpret_cast<const float *>(p)[c];
            break;
        case QAudioFormat::UInt8:
            sample = (quint8(p[c]) - 128) / 128.f;
            break;
        case QAudioFormat::Int32:
            sample = qFromLittleEndian<qint32>(
                reinterpret_cast<const uchar *>(p) + c * 4) / 2147483648.f;
            break;
        default:
            break;
        }
        const float a = qAbs(sample);
        if (a > bestAbs) {
            bestAbs = a;
            best = sample;
        }
    }
    return best;
}

/** Every channel sample in a frame (for density histograms). fn(sample, channelIndex). */
void forEachSampleInFrame(const QByteArray &pcm, const QAudioFormat &format, int frame,
                          const std::function<void(float, int)> &fn)
{
    const int bpf = format.bytesPerFrame();
    if (bpf <= 0 || frame < 0)
        return;
    const int frames = pcm.size() / bpf;
    if (frame >= frames)
        return;
    const int channels = qMax(1, format.channelCount());
    const char *p = pcm.constData() + frame * bpf;
    for (int c = 0; c < channels; ++c) {
        float sample = 0.f;
        switch (format.sampleFormat()) {
        case QAudioFormat::Int16:
            sample = qFromLittleEndian<qint16>(
                reinterpret_cast<const uchar *>(p) + c * 2) / 32768.f;
            break;
        case QAudioFormat::Float:
            sample = reinterpret_cast<const float *>(p)[c];
            break;
        case QAudioFormat::UInt8:
            sample = (quint8(p[c]) - 128) / 128.f;
            break;
        case QAudioFormat::Int32:
            sample = qFromLittleEndian<qint32>(
                reinterpret_cast<const uchar *>(p) + c * 4) / 2147483648.f;
            break;
        default:
            break;
        }
        fn(sample, c);
    }
}

QRgb phosphorStereoRgb(float tL, float tR)
{
    // L → green phosphor, R → cyan; blend by relative energy
    tL = qBound(0.f, tL, 1.f);
    tR = qBound(0.f, tR, 1.f);
    if (tL <= 0.f && tR <= 0.f)
        return qRgb(18, 22, 28);

    auto tone = [](float t, float hueR, float hueG, float hueB, float &r, float &g, float &b) {
        if (t < 0.3f) {
            const float u = t / 0.3f;
            r = hueR * 0.2f * u;
            g = hueG * (0.15f + 0.55f * u);
            b = hueB * (0.15f + 0.45f * u);
        } else if (t < 0.7f) {
            const float u = (t - 0.3f) / 0.4f;
            r = hueR * (0.2f + 0.5f * u);
            g = hueG * (0.7f + 0.25f * u);
            b = hueB * (0.6f + 0.3f * u);
        } else {
            const float u = (t - 0.7f) / 0.3f;
            r = hueR * (0.7f + 0.3f * u);
            g = hueG * (0.95f + 0.05f * u);
            b = hueB * (0.9f + 0.1f * u);
        }
    };

    float rL = 0, gL = 0, bL = 0, rR = 0, gR = 0, bR = 0;
    tone(tL, 0.4f, 1.0f, 0.3f, rL, gL, bL);
    tone(tR, 0.25f, 0.9f, 1.0f, rR, gR, bR);
    const float w = tL + tR;
    const float r = (rL * tL + rR * tR) / w;
    const float g = (gL * tL + gR * tR) / w;
    const float b = (bL * tL + bR * tR) / w;
    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

/** In-place radix-2 Cooley–Tukey FFT (n must be power of two). */
void fftRadix2(QVector<float> &re, QVector<float> &im)
{
    const int n = re.size();
    if (n != im.size() || n < 2)
        return;
    // bit-reverse permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            qSwap(re[i], re[j]);
            qSwap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.f * float(M_PI) / float(len);
        const float wlenRe = std::cos(ang);
        const float wlenIm = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float wRe = 1.f;
            float wIm = 0.f;
            for (int j = 0; j < len / 2; ++j) {
                const float uRe = re[i + j];
                const float uIm = im[i + j];
                const float vRe = re[i + j + len / 2] * wRe - im[i + j + len / 2] * wIm;
                const float vIm = re[i + j + len / 2] * wIm + im[i + j + len / 2] * wRe;
                re[i + j] = uRe + vRe;
                im[i + j] = uIm + vIm;
                re[i + j + len / 2] = uRe - vRe;
                im[i + j + len / 2] = uIm - vIm;
                const float nWRe = wRe * wlenRe - wIm * wlenIm;
                wIm = wRe * wlenIm + wIm * wlenRe;
                wRe = nWRe;
            }
        }
    }
}

int nextPow2(int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

QRgb magToRgb(float t)
{
    // Classic spectrogram ramp: black → indigo → cyan → yellow → white
    t = qBound(0.f, t, 1.f);
    float r, g, b;
    if (t < 0.25f) {
        const float u = t / 0.25f;
        r = 0.05f * u;
        g = 0.05f * u;
        b = 0.15f + 0.55f * u;
    } else if (t < 0.5f) {
        const float u = (t - 0.25f) / 0.25f;
        r = 0.05f + 0.05f * u;
        g = 0.05f + 0.75f * u;
        b = 0.70f + 0.25f * u;
    } else if (t < 0.75f) {
        const float u = (t - 0.5f) / 0.25f;
        r = 0.10f + 0.90f * u;
        g = 0.80f + 0.15f * u;
        b = 0.95f - 0.70f * u;
    } else {
        const float u = (t - 0.75f) / 0.25f;
        r = 1.0f;
        g = 0.95f + 0.05f * u;
        b = 0.25f + 0.75f * u;
    }
    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

QRgb phosphorToRgb(float t)
{
    // Oscilloscope phosphor: near-black → dark green → bright green → yellow-white
    t = qBound(0.f, t, 1.f);
    float r, g, b;
    if (t < 0.15f) {
        const float u = t / 0.15f;
        r = 0.02f * u;
        g = 0.08f + 0.25f * u;
        b = 0.04f * u;
    } else if (t < 0.45f) {
        const float u = (t - 0.15f) / 0.30f;
        r = 0.02f + 0.08f * u;
        g = 0.33f + 0.47f * u;
        b = 0.04f + 0.08f * u;
    } else if (t < 0.75f) {
        const float u = (t - 0.45f) / 0.30f;
        r = 0.10f + 0.55f * u;
        g = 0.80f + 0.15f * u;
        b = 0.12f + 0.10f * u;
    } else {
        const float u = (t - 0.75f) / 0.25f;
        r = 0.65f + 0.35f * u;
        g = 0.95f + 0.05f * u;
        b = 0.22f + 0.55f * u;
    }
    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

} // namespace

QImage waveformDensity(const QByteArray &pcm, const QAudioFormat &format,
                       int timeBins, int ampBins, float scale)
{
    QImage img;
    const int bpf = format.bytesPerFrame();
    if (bpf <= 0 || pcm.isEmpty() || timeBins <= 0 || ampBins < 8)
        return img;

    const int frames = pcm.size() / bpf;
    if (frames < 1)
        return img;

    timeBins = qBound(8, timeBins, 4096);
    ampBins = qBound(16, ampBins, 512);
    scale = qMax(0.f, scale);

    const int channels = qMax(1, format.channelCount());
    const bool stereo = channels >= 2;

    // Per-column amplitude histograms (counts); stereo keeps L/R separate for colour
    QVector<float> countsL(timeBins * ampBins, 0.f);
    QVector<float> countsR(stereo ? timeBins * ampBins : 0, 0.f);
    float maxCount = 0.f;
    const double step = double(frames) / double(timeBins);

    for (int col = 0; col < timeBins; ++col) {
        const int a = int(col * step);
        const int b = qMin(int((col + 1) * step), frames);
        float *colL = countsL.data() + col * ampBins;
        float *colR = stereo ? countsR.data() + col * ampBins : nullptr;
        for (int f = a; f < b; ++f) {
            forEachSampleInFrame(pcm, format, f, [&](float s, int ch) {
                s *= scale;
                const float u = (s + 1.f) * 0.5f;
                int bin = int(u * float(ampBins));
                if (bin < 0)
                    bin = 0;
                else if (bin >= ampBins)
                    bin = ampBins - 1;
                if (!stereo || ch == 0)
                    colL[bin] += 1.f;
                else if (colR)
                    colR[bin] += 1.f;
            });
        }
        for (int row = 0; row < ampBins; ++row) {
            maxCount = qMax(maxCount, colL[row]);
            if (colR)
                maxCount = qMax(maxCount, colR[row]);
        }
    }

    if (maxCount < 1.f)
        maxCount = 1.f;

    const float logMax = std::log1p(maxCount);

    img = QImage(timeBins, ampBins, QImage::Format_RGB32);
    img.fill(qRgb(18, 22, 28));
    for (int col = 0; col < timeBins; ++col) {
        const float *colL = countsL.constData() + col * ampBins;
        const float *colR = stereo ? countsR.constData() + col * ampBins : nullptr;
        for (int row = 0; row < ampBins; ++row) {
            const float cL = colL[row];
            const float cR = colR ? colR[row] : 0.f;
            if (cL <= 0.f && cR <= 0.f)
                continue;
            const float tL = std::log1p(cL) / logMax;
            const float tR = std::log1p(cR) / logMax;
            const int imgRow = ampBins - 1 - row;
            if (stereo)
                img.setPixel(col, imgRow, phosphorStereoRgb(tL, tR));
            else
                img.setPixel(col, imgRow, phosphorToRgb(tL));
        }
    }
    return img;
}


QImage spectrogram(const QByteArray &pcm, const QAudioFormat &format,
                   int timeBins, int fftSize)
{
    QImage img;
    const int bpf = format.bytesPerFrame();
    if (bpf <= 0 || pcm.isEmpty() || timeBins <= 0)
        return img;

    fftSize = nextPow2(qMax(16, fftSize));
    const int frames = pcm.size() / bpf;
    if (frames < 2)
        return img;

    timeBins = qBound(8, timeBins, 2048);
    // Cap work for very long takes: one FFT column per time bin only.
    const int freqBins = fftSize / 2; // positive frequencies, low→high

    QVector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        // Hann
        window[i] = 0.5f * (1.f - std::cos(2.f * float(M_PI) * float(i) / float(fftSize - 1)));
    }

    QVector<float> re(fftSize), im(fftSize);
    // Collect log-magnitudes then normalize for color mapping
    QVector<float> mags(timeBins * freqBins);
    float maxLog = -1e30f;
    const float floorDb = -80.f; // relative silence floor

    for (int col = 0; col < timeBins; ++col) {
        // Center of this time column
        const int center = int((double(col) + 0.5) * double(frames) / double(timeBins));
        const int start = center - fftSize / 2;
        for (int i = 0; i < fftSize; ++i) {
            re[i] = sampleAt(pcm, format, start + i) * window[i];
            im[i] = 0.f;
        }
        fftRadix2(re, im);
        for (int bin = 0; bin < freqBins; ++bin) {
            const float power = re[bin] * re[bin] + im[bin] * im[bin];
            const float db = 10.f * std::log10(power + 1e-20f);
            mags[col * freqBins + bin] = db;
            if (db > maxLog)
                maxLog = db;
        }
    }

    if (maxLog < -200.f)
        maxLog = 0.f;

    img = QImage(timeBins, freqBins, QImage::Format_RGB32);
    for (int col = 0; col < timeBins; ++col) {
        for (int bin = 0; bin < freqBins; ++bin) {
            const float db = mags[col * freqBins + bin];
            // 0 at floorDb below peak, 1 at peak
            const float t = (db - (maxLog + floorDb)) / (-floorDb);
            // Row 0 is top of image; put low frequencies at the bottom
            const int row = freqBins - 1 - bin;
            img.setPixel(col, row, magToRgb(t));
        }
    }
    return img;
}

} // namespace WavFile

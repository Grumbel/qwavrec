// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVFILE_H
#define WAVFILE_H

#include <QAudioFormat>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QImage>

/** Shared WAV header / PCM helpers (Int16/Float/UInt8/Int32). */
namespace WavFile {

struct Info {
    QAudioFormat format;
    QByteArray pcm;
    bool ok = false;
    QString error;
};

Info load(const QString &path);

/** Downsample absolute peaks (about targetBins). Values are 0..1, not normalized. */
QVector<float> peaks(const QByteArray &pcm, const QAudioFormat &format, int targetBins = 400);

/** Peak-normalize Int16 PCM in place; returns false if not Int16 or empty. */
bool peakNormalizeInt16(QByteArray &pcm);

/**
 * Offline magnitude spectrogram (time × frequency), for display only.
 * Columns = time (left→right), rows = frequency (low at bottom).
 * Uses an in-tree real FFT (no extra dependency). Empty image on failure.
 */
QImage spectrogram(const QByteArray &pcm, const QAudioFormat &format,
                   int timeBins = 512, int fftSize = 256);

} // namespace WavFile

#endif

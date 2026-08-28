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

/** Downsample absolute peaks (about targetBins). Values are 0..1, not normalized.
 *  Multi-channel: max abs across channels per bin. */
QVector<float> peaks(const QByteArray &pcm, const QAudioFormat &format, int targetBins = 400);

/** Per-channel absolute peaks (left = ch0, right = ch1 or max of ch≥1).
 *  Mono fills only left; right is empty. Values 0..1, not normalized. */
struct ChannelPeaks {
    QVector<float> left;
    QVector<float> right;
};
ChannelPeaks channelPeaks(const QByteArray &pcm, const QAudioFormat &format, int targetBins = 400);

/** Peak-normalize Int16 PCM in place; returns false if not Int16 or empty. */
bool peakNormalizeInt16(QByteArray &pcm);

/**
 * Intensity-graded waveform image (oscilloscope-style density).
 * Columns = time (left→right), rows = amplitude (−1 at bottom, +1 at top).
 * Each column is a vertical histogram of samples in that time bin; counts are
 * log-scaled and mapped to a phosphor green ramp so sub-pixel activity is visible.
 * @param scale  Multiplier applied to samples before binning (for auto-scale).
 * Empty image on failure.
 */
QImage waveformDensity(const QByteArray &pcm, const QAudioFormat &format,
                       int timeBins = 800, int ampBins = 256, float scale = 1.f);

/**
 * Offline magnitude spectrogram (time × frequency), for display only.
 * Columns = time (left→right), rows = frequency (low at bottom).
 * Uses an in-tree real FFT (no extra dependency). Empty image on failure.
 */
QImage spectrogram(const QByteArray &pcm, const QAudioFormat &format,
                   int timeBins = 512, int fftSize = 256);

} // namespace WavFile

#endif

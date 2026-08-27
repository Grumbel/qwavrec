// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>

/**
 * Static document waveform with an optional playhead.
 * Does not scroll or animate on its own — only changes when setPeaks /
 * setPlaybackPosition / clear are called.
 */
class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setPeaks(const QVector<float> &peaks);
    void clear();
    void setPlaybackPosition(qreal pos); // 0..1

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QVector<float> m_peaks;
    qreal m_playbackPos = 0.0;
};

#endif

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>

/**
 * Displays either:
 *  - a static waveform of the current document (peaks), with a playhead, or
 *  - a short live scrolling level history when no document is loaded.
 */
class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    /** Replace the static document waveform (empty = clear). */
    void setPeaks(const QVector<float> &peaks);

    /** Clear both static peaks and live history. */
    void clear();

    /** Playhead position 0..1 over the static waveform. */
    void setPlaybackPosition(qreal pos);

    /** Feed a live level sample (only used when there are no static peaks). */
    void addLiveLevel(qreal level);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QVector<float> m_peaks;       // static document waveform, 0..1
    QVector<float> m_live;        // short scrolling history when no peaks
    static const int LiveSize = 128;
    qreal m_playbackPos = 0.0;
};

#endif

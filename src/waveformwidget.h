// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QPixmap>

class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    enum class DisplayMode { Waveform, Spectrogram };

    explicit WaveformWidget(QWidget *parent = nullptr);

    void setPeaks(const QVector<float> &peaks);
    /**
     * Intensity-graded waveform (time →, amplitude ↑). Empty clears density.
     * Preferred over setPeaks silhouette when non-null.
     */
    void setWaveformDensity(const QImage &image);
    /** Offline spectrogram image (time →, frequency ↑). Empty clears spectrogram. */
    void setSpectrogram(const QImage &image);
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_mode; }
    void clear();
    void setPlaybackPosition(qreal pos); // 0..1

    /** Usable content width in pixels (inner rect); used for peak-bin sizing. */
    int contentWidth() const;

    /** Selection as 0..1 range; invalid if end <= start. */
    bool hasSelection() const { return m_selEnd > m_selStart + 1e-6; }
    qreal selectionStart() const { return m_selStart; }
    qreal selectionEnd() const { return m_selEnd; }
    void setSelection(qreal a, qreal b);
    void clearSelection();

signals:
    void seekRequested(qreal pos);
    void selectionChanged(qreal start, qreal end); // end<=start means cleared
    void contextMenuRequested(const QPoint &globalPos);
    /** Emitted when the drawable width changes so the host can rebuild peaks. */
    void contentWidthChanged(int width);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    enum class Hit { None, EdgeA, EdgeB, Interior };
    enum class Drag { None, NewSelection, EdgeA, EdgeB };

    qreal posFromX(qreal x) const;
    qreal xFromPos(qreal pos) const;
    Hit hitTest(qreal x) const;
    void setHover(Hit h);
    void invalidateBodyCache();
    void ensureBodyCache();
    void paintWaveform(QPainter &p, const QRect &r);
    void paintSpectrogram(QPainter &p, const QRect &r);
    void paintOverlay(QPainter &p, const QRect &r);
    /** Edge grab distance in widget pixels. */
    static constexpr qreal kEdgePx = 8.0;

    DisplayMode m_mode = DisplayMode::Waveform;
    QVector<float> m_peaks;
    /** Oscilloscope-style density image; used in Waveform mode when non-null. */
    QImage m_waveDensity;
    QImage m_spectrogram;
    /** Cached body (wave or spectrogram) without selection/playhead. */
    QPixmap m_bodyCache;
    bool m_bodyCacheDirty = true;
    qreal m_playbackPos = 0.0;
    qreal m_selStart = 0.0;
    qreal m_selEnd = 0.0;
    Drag m_drag = Drag::None;
    qreal m_dragAnchor = 0.0;
    bool m_moved = false;
    Hit m_hover = Hit::None;
};

#endif

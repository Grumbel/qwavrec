// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>

class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setPeaks(const QVector<float> &peaks);
    void clear();
    void setPlaybackPosition(qreal pos); // 0..1

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

protected:
    void paintEvent(QPaintEvent *event) override;
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
    /** Edge grab distance in widget pixels. */
    static constexpr qreal kEdgePx = 8.0;

    QVector<float> m_peaks;
    qreal m_playbackPos = 0.0;
    qreal m_selStart = 0.0;
    qreal m_selEnd = 0.0;
    Drag m_drag = Drag::None;
    qreal m_dragAnchor = 0.0;
    bool m_moved = false;
    Hit m_hover = Hit::None;
};

#endif

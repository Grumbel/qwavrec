// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waveformwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QMouseEvent>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(64);
    setMinimumWidth(200);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void WaveformWidget::setPeaks(const QVector<float> &peaks)
{
    if (m_peaks == peaks)
        return;
    m_peaks = peaks;
    update();
}

void WaveformWidget::clear()
{
    m_peaks.clear();
    m_playbackPos = 0.0;
    clearSelection();
    update();
}

void WaveformWidget::setPlaybackPosition(qreal pos)
{
    const qreal p = qBound(0.0, pos, 1.0);
    if (qFuzzyCompare(p + 1.0, m_playbackPos + 1.0))
        return;
    m_playbackPos = p;
    update();
}

void WaveformWidget::setSelection(qreal a, qreal b)
{
    a = qBound(0.0, a, 1.0);
    b = qBound(0.0, b, 1.0);
    if (a > b)
        qSwap(a, b);
    m_selStart = a;
    m_selEnd = b;
    update();
    emit selectionChanged(m_selStart, m_selEnd);
}

void WaveformWidget::clearSelection()
{
    if (!hasSelection() && m_selStart == 0.0 && m_selEnd == 0.0)
        return;
    m_selStart = 0.0;
    m_selEnd = 0.0;
    update();
    emit selectionChanged(0.0, 0.0);
}

qreal WaveformWidget::posFromX(qreal x) const
{
    const QRect r = rect().adjusted(1, 1, -1, -1);
    if (r.width() <= 0)
        return 0.0;
    return qBound(0.0, (x - r.left()) / r.width(), 1.0);
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_peaks.isEmpty()) {
        m_dragging = true;
        m_moved = false;
        m_dragAnchor = posFromX(event->position().x());
        m_selStart = m_dragAnchor;
        m_selEnd = m_dragAnchor;
        update();
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && !m_peaks.isEmpty()) {
        const qreal p = posFromX(event->position().x());
        if (qAbs(p - m_dragAnchor) > 0.005)
            m_moved = true;
        m_selStart = qMin(m_dragAnchor, p);
        m_selEnd = qMax(m_dragAnchor, p);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (!m_moved) {
            // Click without drag → seek and clear selection
            clearSelection();
            m_playbackPos = m_dragAnchor;
            update();
            emit seekRequested(m_dragAnchor);
        } else if (hasSelection()) {
            emit selectionChanged(m_selStart, m_selEnd);
            // Seek to region start
            m_playbackPos = m_selStart;
            update();
            emit seekRequested(m_selStart);
        } else {
            clearSelection();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        clearSelection();
    }
    QWidget::mouseDoubleClickEvent(event);
}

QSize WaveformWidget::sizeHint() const { return QSize(400, 80); }
QSize WaveformWidget::minimumSizeHint() const { return QSize(200, 48); }

void WaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(rect(), QColor(10, 20, 15));
    p.setPen(QColor(40, 80, 50));
    p.drawRect(r);

    p.setPen(QPen(QColor(20, 50, 30), 1, Qt::DotLine));
    const int midY = r.center().y();
    p.drawLine(r.left(), midY, r.right(), midY);

    if (m_peaks.isEmpty()) {
        p.setPen(QColor(80, 120, 90));
        p.drawText(r, Qt::AlignCenter, tr("No recording"));
        return;
    }

    // Selection highlight
    if (hasSelection()) {
        const int x0 = r.left() + int(m_selStart * r.width());
        const int x1 = r.left() + int(m_selEnd * r.width());
        p.fillRect(QRect(x0, r.top(), qMax(1, x1 - x0), r.height()),
                    QColor(255, 200, 50, 50));
        p.setPen(QPen(QColor(255, 180, 40), 1));
        p.drawLine(x0, r.top(), x0, r.bottom());
        p.drawLine(x1, r.top(), x1, r.bottom());
        // A / B labels
        p.setPen(QColor(255, 220, 80));
        p.setFont(QFont(font().family(), 9, QFont::Bold));
        p.drawText(x0 + 2, r.top() + 12, QStringLiteral("A"));
        p.drawText(x1 - 10, r.top() + 12, QStringLiteral("B"));
    }

    const int n = m_peaks.size();
    const qreal step = r.width() / static_cast<qreal>(qMax(1, n));

    QPainterPath path;
    path.moveTo(r.left(), midY);
    for (int i = 0; i < n; ++i) {
        const qreal v = m_peaks.at(i);
        const qreal x = r.left() + (i + 0.5) * step;
        path.lineTo(x, midY - (r.height() / 2.0 - 3) * v);
    }
    for (int i = n - 1; i >= 0; --i) {
        const qreal v = m_peaks.at(i);
        const qreal x = r.left() + (i + 0.5) * step;
        path.lineTo(x, midY + (r.height() / 2.0 - 3) * v);
    }
    path.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 160, 70, 120));
    p.drawPath(path);
    p.setPen(QPen(QColor(80, 230, 120), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    if (m_playbackPos >= 0.0 && m_playbackPos <= 1.0) {
        const int cx = r.left() + static_cast<int>(m_playbackPos * r.width());
        p.setPen(QPen(QColor(255, 200, 50), 2));
        p.drawLine(cx, r.top() + 1, cx, r.bottom() - 1);
    }
}

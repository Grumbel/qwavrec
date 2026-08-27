// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waveformwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(64);
    setMinimumWidth(200);
}

void WaveformWidget::setPeaks(const QVector<float> &peaks)
{
    if (m_peaks == peaks)
        return; // avoid needless repaint when unchanged
    m_peaks = peaks;
    update();
}

void WaveformWidget::clear()
{
    if (m_peaks.isEmpty() && m_playbackPos == 0.0)
        return;
    m_peaks.clear();
    m_playbackPos = 0.0;
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
    for (int i = 1; i < 4; ++i) {
        const int y = r.top() + (r.height() * i) / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }

    if (m_peaks.isEmpty()) {
        p.setPen(QColor(80, 120, 90));
        p.drawText(r, Qt::AlignCenter, tr("No recording"));
        return;
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

    if (m_playbackPos > 0.0 && m_playbackPos < 1.0) {
        const int cx = r.left() + static_cast<int>(m_playbackPos * r.width());
        p.setPen(QPen(QColor(255, 200, 50), 2));
        p.drawLine(cx, r.top() + 1, cx, r.bottom() - 1);
    }
}

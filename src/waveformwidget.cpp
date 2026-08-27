// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waveformwidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(64);
    setMinimumWidth(200);
    m_history.reserve(HistorySize);
}

void WaveformWidget::addLevel(qreal level)
{
    m_history.append(qBound(0.0, level, 1.0));
    while (m_history.size() > HistorySize)
        m_history.removeFirst();
    update();
}

void WaveformWidget::clear()
{
    m_history.clear();
    m_playbackPos = 0.0;
    update();
}

void WaveformWidget::setPlaybackPosition(qreal pos)
{
    m_playbackPos = qBound(0.0, pos, 1.0);
    update();
}

void WaveformWidget::setDurationMs(qint64 ms) { m_durationMs = ms; }

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
        int y = r.top() + (r.height() * i) / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }

    if (m_history.isEmpty()) {
        p.setPen(QColor(80, 120, 90));
        p.drawText(r, Qt::AlignCenter, tr("No signal"));
        return;
    }

    QPainterPath path;
    const int n = m_history.size();
    const qreal step = r.width() / static_cast<qreal>(qMax(1, n - 1));
    for (int i = 0; i < n; ++i) {
        const qreal v = m_history[i];
        const qreal x = r.left() + i * step;
        const qreal halfH = (r.height() / 2.0 - 2) * v;
        const qreal y = midY - halfH;
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
    }
    p.setPen(QPen(QColor(0, 220, 80, 80), 3));
    p.drawPath(path);
    p.setPen(QPen(QColor(100, 255, 140), 1.5));
    p.drawPath(path);

    QPainterPath path2;
    for (int i = 0; i < n; ++i) {
        const qreal v = m_history[i];
        const qreal x = r.left() + i * step;
        const qreal halfH = (r.height() / 2.0 - 2) * v;
        const qreal y = midY + halfH;
        if (i == 0) path2.moveTo(x, y);
        else path2.lineTo(x, y);
    }
    p.setPen(QPen(QColor(0, 180, 60, 60), 2));
    p.drawPath(path2);
    p.setPen(QPen(QColor(80, 220, 120, 180), 1));
    p.drawPath(path2);

    if (m_playbackPos > 0.0 && m_playbackPos < 1.0) {
        const int cx = r.left() + static_cast<int>(m_playbackPos * r.width());
        p.setPen(QPen(QColor(255, 200, 50), 2));
        p.drawLine(cx, r.top(), cx, r.bottom());
    }
}

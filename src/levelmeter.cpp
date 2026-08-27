// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "levelmeter.h"
#include <QPainter>
#include <QPaintEvent>

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(16);
    setMinimumWidth(80);
}

void LevelMeter::setLevel(qreal level)
{
    m_level = qBound(0.0, level, 1.0);
    if (m_level > m_peakHold)
        m_peakHold = m_level;
    else
        m_peakHold = qMax(0.0, m_peakHold - 0.02);
    update();
}

void LevelMeter::setPeak(qreal peak)
{
    m_peak = qBound(0.0, peak, 1.0);
    update();
}

QSize LevelMeter::sizeHint() const { return QSize(120, 20); }
QSize LevelMeter::minimumSizeHint() const { return QSize(60, 14); }

void LevelMeter::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRect r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(r, QColor(20, 20, 30));
    p.setPen(QColor(60, 60, 80));
    p.drawRect(r);

    const int segments = 20;
    const int gap = 1;
    const int segW = (r.width() - (segments - 1) * gap) / segments;
    if (segW < 2) {
        int w = static_cast<int>(r.width() * m_level);
        QLinearGradient g(r.topLeft(), r.topRight());
        g.setColorAt(0.0, QColor(0, 180, 0));
        g.setColorAt(0.6, QColor(220, 220, 0));
        g.setColorAt(0.85, QColor(220, 80, 0));
        g.setColorAt(1.0, QColor(200, 0, 0));
        p.fillRect(r.x(), r.y(), w, r.height(), g);
        return;
    }

    for (int i = 0; i < segments; ++i) {
        const qreal thresh = (i + 1) / static_cast<qreal>(segments);
        const int x = r.x() + i * (segW + gap);
        QRect seg(x, r.y() + 1, segW, r.height() - 2);
        QColor color = (thresh < 0.6) ? QColor(0, 160, 0)
                     : (thresh < 0.85) ? QColor(200, 180, 0)
                     : QColor(200, 30, 30);
        if (m_level >= thresh)
            p.fillRect(seg, color);
        else
            p.fillRect(seg, color.darker(280));
        if (m_peakHold >= thresh && m_peakHold < thresh + 1.0 / segments)
            p.fillRect(seg.adjusted(0, 0, 0, -seg.height() / 2), Qt::white);
    }
}

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

void LevelMeter::updatePeakHold(qreal level, qreal &hold)
{
    if (level > hold)
        hold = level;
    else
        hold = qMax(0.0, hold - 0.02);
}

void LevelMeter::setLevel(qreal level)
{
    m_levelL = qBound(0.0, level, 1.0);
    m_levelR = m_levelL;
    updatePeakHold(m_levelL, m_peakHoldL);
    m_peakHoldR = m_peakHoldL;
    update();
}

void LevelMeter::setLevels(qreal left, qreal right)
{
    m_levelL = qBound(0.0, left, 1.0);
    m_levelR = qBound(0.0, right, 1.0);
    updatePeakHold(m_levelL, m_peakHoldL);
    updatePeakHold(m_levelR, m_peakHoldR);
    update();
}

void LevelMeter::setStereo(bool on)
{
    if (m_stereo == on)
        return;
    m_stereo = on;
    update();
}

void LevelMeter::setPeak(qreal peak)
{
    m_peak = qBound(0.0, peak, 1.0);
    update();
}

QSize LevelMeter::sizeHint() const { return QSize(120, 20); }
QSize LevelMeter::minimumSizeHint() const { return QSize(60, 14); }

void LevelMeter::paintBar(QPainter &p, const QRect &r, qreal level, qreal peakHold)
{
    p.fillRect(r, QColor(20, 20, 30));
    p.setPen(QColor(60, 60, 80));
    p.drawRect(r);

    const int segments = 20;
    const int gap = 1;
    const int segW = (r.width() - (segments - 1) * gap) / segments;
    if (segW < 2) {
        int w = static_cast<int>(r.width() * level);
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
        if (level >= thresh)
            p.fillRect(seg, color);
        else
            p.fillRect(seg, color.darker(280));
        if (peakHold >= thresh && peakHold < thresh + 1.0 / segments)
            p.fillRect(seg.adjusted(0, 0, 0, -seg.height() / 2), Qt::white);
    }
}

void LevelMeter::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRect outer = rect().adjusted(1, 1, -1, -1);

    if (!m_stereo) {
        paintBar(p, outer, m_levelL, m_peakHoldL);
        return;
    }

    // Split L | R with a thin gap and channel labels
    const int gap = 3;
    const int half = (outer.width() - gap) / 2;
    const QRect leftR(outer.x(), outer.y(), half, outer.height());
    const QRect rightR(outer.x() + half + gap, outer.y(), outer.width() - half - gap, outer.height());
    paintBar(p, leftR, m_levelL, m_peakHoldL);
    paintBar(p, rightR, m_levelR, m_peakHoldR);

    p.setPen(QColor(140, 150, 160));
    QFont f = font();
    f.setPointSize(qMax(7, f.pointSize() - 2));
    p.setFont(f);
    p.drawText(leftR.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("L"));
    p.drawText(rightR.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("R"));
}

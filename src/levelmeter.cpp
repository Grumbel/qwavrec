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

QSize LevelMeter::sizeHint() const
{
    // Tall enough to span device + gain/volume rows in the I/O panel.
    return m_stereo ? QSize(140, 56) : QSize(140, 48);
}

QSize LevelMeter::minimumSizeHint() const
{
    return m_stereo ? QSize(80, 40) : QSize(80, 36);
}

void LevelMeter::paintBar(QPainter &p, const QRect &r, qreal level, qreal peakHold)
{
    p.fillRect(r, QColor(20, 20, 30));
    p.setPen(QColor(60, 60, 80));
    p.drawRect(r);

    const int segments = 20;
    const int gap = 1;
    const int innerW = r.width() - 2;
    const int segW = (innerW - (segments - 1) * gap) / segments;
    if (segW < 2) {
        const int w = static_cast<int>((r.width() - 2) * level);
        QLinearGradient g(r.topLeft(), r.topRight());
        g.setColorAt(0.0, QColor(0, 180, 0));
        g.setColorAt(0.6, QColor(220, 220, 0));
        g.setColorAt(0.85, QColor(220, 80, 0));
        g.setColorAt(1.0, QColor(200, 0, 0));
        p.fillRect(r.x() + 1, r.y() + 1, w, r.height() - 2, g);
        // Peak hold: thin full-height mark
        if (peakHold > 0.01) {
            const int px = r.x() + 1 + static_cast<int>((r.width() - 2) * peakHold);
            p.fillRect(px - 1, r.y() + 1, 2, r.height() - 2, QColor(240, 240, 240));
        }
        return;
    }

    int peakSeg = -1;
    if (peakHold > 0.01)
        peakSeg = qBound(0, int(peakHold * segments) - 1, segments - 1);

    for (int i = 0; i < segments; ++i) {
        const qreal thresh = (i + 1) / static_cast<qreal>(segments);
        const int x = r.x() + 1 + i * (segW + gap);
        QRect seg(x, r.y() + 1, segW, r.height() - 2);
        QColor color = (thresh < 0.6) ? QColor(0, 160, 0)
                     : (thresh < 0.85) ? QColor(200, 180, 0)
                     : QColor(200, 30, 30);
        if (level >= thresh)
            p.fillRect(seg, color);
        else
            p.fillRect(seg, color.darker(280));
    }
    // Peak hold: narrow full-height tick at the held segment (not a half-height block)
    if (peakSeg >= 0) {
        const int x = r.x() + 1 + peakSeg * (segW + gap);
        const int tickW = qMax(2, segW / 3);
        const int tickX = x + (segW - tickW) / 2;
        p.fillRect(tickX, r.y() + 1, tickW, r.height() - 2, QColor(240, 240, 240));
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

    // Stacked L (top) / R (bottom)
    const int gap = 2;
    const int half = (outer.height() - gap) / 2;
    const QRect topR(outer.x(), outer.y(), outer.width(), half);
    const QRect botR(outer.x(), outer.y() + half + gap, outer.width(), outer.height() - half - gap);
    paintBar(p, topR, m_levelL, m_peakHoldL);
    paintBar(p, botR, m_levelR, m_peakHoldR);

    p.setPen(QColor(160, 170, 180));
    QFont f = font();
    f.setPointSize(qMax(7, f.pointSize() - 2));
    p.setFont(f);
    p.drawText(topR.adjusted(3, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("L"));
    p.drawText(botR.adjusted(3, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("R"));
}

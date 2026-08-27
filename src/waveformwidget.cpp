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
    m_live.reserve(LiveSize);
}

void WaveformWidget::setPeaks(const QVector<float> &peaks)
{
    m_peaks = peaks;
    m_live.clear();
    m_playbackPos = 0.0;
    update();
}

void WaveformWidget::clear()
{
    m_peaks.clear();
    m_live.clear();
    m_playbackPos = 0.0;
    update();
}

void WaveformWidget::setPlaybackPosition(qreal pos)
{
    m_playbackPos = qBound(0.0, pos, 1.0);
    update();
}

void WaveformWidget::addLiveLevel(qreal level)
{
    // Only scroll when we have no static document waveform
    if (!m_peaks.isEmpty())
        return;

    m_live.append(qBound(0.0f, float(level), 1.0f));
    while (m_live.size() > LiveSize)
        m_live.removeFirst();
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

    // Grid
    p.setPen(QPen(QColor(20, 50, 30), 1, Qt::DotLine));
    const int midY = r.center().y();
    p.drawLine(r.left(), midY, r.right(), midY);
    for (int i = 1; i < 4; ++i) {
        const int y = r.top() + (r.height() * i) / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }

    const QVector<float> *data = !m_peaks.isEmpty() ? &m_peaks : &m_live;
    if (data->isEmpty()) {
        p.setPen(QColor(80, 120, 90));
        p.drawText(r, Qt::AlignCenter,
                   m_peaks.isEmpty() ? tr("No recording") : tr("No signal"));
        return;
    }

    const int n = data->size();
    const qreal step = r.width() / static_cast<qreal>(qMax(1, n));

    // Filled waveform (mirrored around center) — classic recorder look
    QPainterPath path;
    path.moveTo(r.left(), midY);
    for (int i = 0; i < n; ++i) {
        const qreal v = data->at(i);
        const qreal x = r.left() + (i + 0.5) * step;
        const qreal halfH = (r.height() / 2.0 - 3) * v;
        path.lineTo(x, midY - halfH);
    }
    for (int i = n - 1; i >= 0; --i) {
        const qreal v = data->at(i);
        const qreal x = r.left() + (i + 0.5) * step;
        const qreal halfH = (r.height() / 2.0 - 3) * v;
        path.lineTo(x, midY + halfH);
    }
    path.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 160, 70, 120));
    p.drawPath(path);

    p.setPen(QPen(QColor(80, 230, 120), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Playhead only for static document waveform
    if (!m_peaks.isEmpty() && m_playbackPos >= 0.0 && m_playbackPos <= 1.0) {
        const int cx = r.left() + static_cast<int>(m_playbackPos * r.width());
        p.setPen(QPen(QColor(255, 200, 50), 2));
        p.drawLine(cx, r.top() + 1, cx, r.bottom() - 1);
    }

    // Mode hint in corner
    p.setPen(QColor(60, 100, 70));
    p.setFont(QFont(font().family(), 8));
    if (!m_peaks.isEmpty())
        p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft, tr("Document"));
    else
        p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft, tr("Monitor"));
}

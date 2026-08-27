// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markedslider.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QMouseEvent>

MarkedSlider::MarkedSlider(Qt::Orientation o, QWidget *parent)
    : QSlider(o, parent)
{
}

void MarkedSlider::setMarkerValue(int value)
{
    m_marker = value;
    update();
}

void MarkedSlider::paintEvent(QPaintEvent *event)
{
    QSlider::paintEvent(event);
    if (m_marker < minimum() || m_marker > maximum())
        return;

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect gr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect hr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    const double span = maximum() - minimum();
    if (span <= 0)
        return;
    const double t = (m_marker - minimum()) / span;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    if (orientation() == Qt::Horizontal) {
        const int usable = gr.width() - hr.width();
        const int x = gr.x() + hr.width() / 2 + int(t * usable);
        p.setPen(QPen(QColor(220, 180, 40), 2));
        p.drawLine(x, gr.top(), x, gr.bottom());
    } else {
        const int usable = gr.height() - hr.height();
        const int y = gr.bottom() - hr.height() / 2 - int(t * usable);
        p.setPen(QPen(QColor(220, 180, 40), 2));
        p.drawLine(gr.left(), y, gr.right(), y);
    }
}

SeekSlider::SeekSlider(Qt::Orientation o, QWidget *parent)
    : QSlider(o, parent)
{
    setTracking(true);
}

void SeekSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && maximum() > minimum()) {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect handle = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
        // Click on the groove (not the handle) → jump absolutely.
        // Stock QSlider would only page-step (~10 ms on a ms-range timeline).
        if (!handle.contains(event->position().toPoint())) {
            const int pos = (orientation() == Qt::Horizontal)
                ? int(event->position().x())
                : int(event->position().y());
            const int span = (orientation() == Qt::Horizontal) ? width() : height();
            const int val = QStyle::sliderValueFromPosition(
                minimum(), maximum(), pos, span, invertedAppearance());
            setValue(val);
            event->accept();
            return;
        }
    }
    QSlider::mousePressEvent(event);
}

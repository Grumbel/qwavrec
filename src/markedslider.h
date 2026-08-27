// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MARKEDSLIDER_H
#define MARKEDSLIDER_H

#include <QSlider>

/** QSlider with an optional fixed marker (e.g. unity gain at 100%). */
class MarkedSlider : public QSlider
{
    Q_OBJECT
public:
    explicit MarkedSlider(Qt::Orientation o, QWidget *parent = nullptr);
    void setMarkerValue(int value); // in slider units; < minimum() to hide

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_marker = -1;
};

/**
 * Timeline slider: click on the groove jumps to that absolute position
 * (stock QSlider only page-steps by a few units, which is useless for ms ranges).
 */
class SeekSlider : public QSlider
{
    Q_OBJECT
public:
    explicit SeekSlider(Qt::Orientation o, QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

#endif

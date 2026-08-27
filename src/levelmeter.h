// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LEVELMETER_H
#define LEVELMETER_H

#include <QWidget>

class LevelMeter : public QWidget
{
    Q_OBJECT

public:
    explicit LevelMeter(QWidget *parent = nullptr);

    void setLevel(qreal level);
    void setPeak(qreal peak);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    qreal m_level = 0.0;
    qreal m_peak = 0.0;
    qreal m_peakHold = 0.0;
};

#endif

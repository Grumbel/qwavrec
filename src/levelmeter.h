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

    /** Mono: single bar from @p level. */
    void setLevel(qreal level);
    /** Stereo: split bar L | R. */
    void setLevels(qreal left, qreal right);
    void setStereo(bool on);
    bool isStereo() const { return m_stereo; }
    void setPeak(qreal peak);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void paintBar(QPainter &p, const QRect &r, qreal level, qreal peakHold);
    void updatePeakHold(qreal level, qreal &hold);

    bool m_stereo = false;
    qreal m_levelL = 0.0;
    qreal m_levelR = 0.0;
    qreal m_peak = 0.0;
    qreal m_peakHoldL = 0.0;
    qreal m_peakHoldR = 0.0;
};

#endif

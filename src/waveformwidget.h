// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>

class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void addLevel(qreal level);
    void clear();
    void setPlaybackPosition(qreal pos);
    void setDurationMs(qint64 ms);
protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
private:
    QVector<qreal> m_history;
    static const int HistorySize = 256;
    qreal m_playbackPos = 0.0;
    qint64 m_durationMs = 0;
};

#endif

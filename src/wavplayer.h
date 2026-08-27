// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WAVPLAYER_H
#define WAVPLAYER_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <QTimer>

/**
 * Low-level PCM WAV player via QAudioSink.
 * Loads PCM into memory; suitable for typical short recordings.
 */
class WavPlayer : public QObject
{
    Q_OBJECT
public:
    enum State { Stopped, Playing, Paused };

    explicit WavPlayer(QObject *parent = nullptr);
    ~WavPlayer() override;

    bool load(const QString &path);
    void play();
    void pause();
    void stop();
    void setPosition(qint64 ms);
    void setVolume(qreal volume); // 0..1
    void setDevice(const QAudioDevice &device);

    State state() const { return m_state; }
    qint64 position() const;
    qint64 duration() const { return m_durationMs; }
    bool isLoaded() const { return !m_pcm.isEmpty(); }
    QString path() const { return m_path; }
    QAudioFormat format() const { return m_format; }

signals:
    void stateChanged(WavPlayer::State state);
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void errorOccurred(const QString &message);

private slots:
    void onSinkStateChanged(QAudio::State state);
    void tickPosition();

private:
    bool parseAndLoad(const QString &path);
    void setState(State s);
    void recreateSink();

    QString m_path;
    QByteArray m_pcm;
    QAudioFormat m_format;
    QAudioDevice m_device;
    QAudioSink *m_sink = nullptr;
    QBuffer m_buffer;
    QTimer m_posTimer;

    State m_state = Stopped;
    qint64 m_durationMs = 0;
    qreal m_volume = 0.8;
    qint64 m_pauseOffset = 0; // bytes into pcm when paused
};

#endif

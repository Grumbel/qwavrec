// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PULSEBACKEND_H
#define PULSEBACKEND_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QAudioFormat>
#include <QThread>
#include <QMutex>
#include <QVector>
#include <atomic>

struct PulseDevice {
    QString name;        // PulseAudio name (stable id)
    QString description; // human-readable
    bool isMonitor = false;
    bool isDefault = false;
};

/** Enumerate sources (including monitors) and sinks via libpulse. */
class PulseDevices
{
public:
    static QVector<PulseDevice> sources();
    static QVector<PulseDevice> sinks();
    static QString defaultSourceName();
    static QString defaultSinkName();
};

/**
 * Capture thread using pa_simple (RECORD).
 * Emits PCM Int16 interleaved chunks; sample rate / channels fixed at open.
 */
class PulseCapture : public QObject
{
    Q_OBJECT
public:
    explicit PulseCapture(QObject *parent = nullptr);
    ~PulseCapture() override;

    bool start(const QString &sourceName, int sampleRate = 48000, int channels = 1);
    void stop();
    bool isRunning() const { return m_running.load(); }
    QAudioFormat format() const { return m_format; }
    void setGain(qreal gain) { m_gain = gain; } // 0..3

signals:
    void samplesReady(const QByteArray &pcm, float peak);
    void errorOccurred(const QString &message);
    void stopped();

private:
    void runLoop(QString sourceName, int session);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    QThread m_thread;
    QAudioFormat m_format;
    qreal m_gain = 1.0;
    std::atomic<int> m_session{0};
};

/**
 * Playback of in-memory Int16 (or Float) PCM via pa_simple.
 */
class PulsePlayback : public QObject
{
    Q_OBJECT
public:
    enum State { Stopped, Playing, Paused };
    Q_ENUM(State)

    explicit PulsePlayback(QObject *parent = nullptr);
    ~PulsePlayback() override;

    bool loadPcm(const QByteArray &pcm, const QAudioFormat &format);
    void play();
    void pause();
    void stop();
    void setPosition(qint64 ms);
    void setVolume(qreal volume); // 0..1 — software scale
    void setSinkName(const QString &name);
    void setLoop(bool loop) { m_loop = loop; }
    /** Limit playback to [startMs, endMs]; use endMs<=startMs for full file. */
    void setPlayRange(qint64 startMs, qint64 endMs);
    void clearPlayRange();

    State state() const { return m_state; }
    qint64 position() const;
    qint64 duration() const { return m_durationMs; }
    qreal levelAtPosition(qint64 ms) const;

signals:
    void stateChanged(PulsePlayback::State state);
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void errorOccurred(const QString &message);

private:
    void runLoop();
    void setState(State s);
    qint64 msToBytes(qint64 ms) const;
    qint64 bytesToMs(qint64 bytes) const;

    QByteArray m_pcm;
    QAudioFormat m_format;
    QString m_sinkName;
    State m_state = Stopped;
    qint64 m_durationMs = 0;
    qint64 m_byteOffset = 0;
    qreal m_volume = 0.8;
    bool m_loop = false;
    qint64 m_rangeStartMs = 0;
    qint64 m_rangeEndMs = -1; // -1 = full duration

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_pause{false};
    QThread m_thread;
    mutable QMutex m_mutex;
};

#endif


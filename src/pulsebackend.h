// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PULSEBACKEND_H
#define PULSEBACKEND_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QAudioFormat>
#include <QMutex>
#include <QVector>
#include <atomic>

struct PulseDevice {
    QString name;
    QString description;
    bool isMonitor = false;
    bool isDefault = false;
};

/**
 * Enumerate sources (including monitors) and sinks.
 * Uses a single PulseAudio connection; safe to call from the GUI thread
 * but prefer not to call it in a tight loop.
 */
class PulseDevices
{
public:
    struct Lists {
        QVector<PulseDevice> sources;
        QVector<PulseDevice> sinks;
        QString defaultSource;
        QString defaultSink;
        bool ok = false;
    };

    /** One connection: server info + source list + sink list. */
    static Lists query();

    static QVector<PulseDevice> sources() { return query().sources; }
    static QVector<PulseDevice> sinks() { return query().sinks; }
};

/**
 * Capture via pa_simple on a worker thread.
 *
 * - Meter peaks are stored lock-free for a GUI timer (no event flood).
 * - PCM for recording is queued; drain with takeRecordedAudio().
 * - stop() is non-blocking (worker exits after the current read).
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
    void setGain(qreal gain) { m_gain.store(gain); }

    /** Latest peak 0..1 for the input meter (lock-free). */
    qreal currentPeak() const { return m_peak.load(); }

    /** Drain PCM accumulated since last call (for recording). */
    QByteArray takeRecordedAudio();

    void setRecording(bool on);

signals:
    void errorOccurred(const QString &message);
    void started();
    void stopped();

private:
    void runLoop(QString sourceName, int session);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_recording{false};
    std::atomic<int> m_session{0};
    std::atomic<qreal> m_gain{1.0};
    std::atomic<qreal> m_peak{0.0};

    QAudioFormat m_format;
    QMutex m_pcmMutex;
    QByteArray m_pcmQueue;
};

/**
 * Playback of in-memory PCM via pa_simple on a worker thread.
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
    void setVolume(qreal volume);
    void setSinkName(const QString &name);
    void setLoop(bool loop) { m_loop = loop; }
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
    void runLoop(int generation);
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
    qint64 m_rangeEndMs = -1;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_pause{false};
    std::atomic<int> m_generation{0};
    mutable QMutex m_mutex;
};

#endif

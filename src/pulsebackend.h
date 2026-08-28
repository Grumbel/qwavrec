// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PULSEBACKEND_H
#define PULSEBACKEND_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QAudioFormat>
#include <QMutex>
#include <QThread>
#include <QVector>
#include <atomic>

struct PulseDevice {
    QString name;
    QString description;
    bool isMonitor = false;
    bool isDefault = false;
    /** Channel count from the source/sink sample spec (1 = mono, 2 = stereo, …). */
    int channelCount = 1;
};

/**
 * Snapshot of Pulse sources/sinks (including monitor sources).
 */
struct PulseDeviceLists {
    QVector<PulseDevice> sources;
    QVector<PulseDevice> sinks;
    QString defaultSource;
    QString defaultSink;
    bool ok = false;
};

/**
 * One-shot enumeration (single pa_context connect/disconnect).
 * Prefer PulseDeviceWatcher for ongoing UI; use this only for rare
 * synchronous needs or as a fallback before the watcher is ready.
 */
class PulseDevices
{
public:
    using Lists = PulseDeviceLists;

    /** One connection: server info + source list + sink list. */
    static Lists query();

    static QVector<PulseDevice> sources() { return query().sources; }
    static QVector<PulseDevice> sinks() { return query().sinks; }
};

/**
 * Long-lived Pulse context on a worker thread: subscribe to source/sink/server
 * changes and re-enumerate. Emits devicesChanged() (queued) when the cache
 * updates. Never call Pulse I/O from the GUI thread via this class.
 */
class PulseDeviceWatcher : public QObject
{
    Q_OBJECT
public:
    explicit PulseDeviceWatcher(QObject *parent = nullptr);
    ~PulseDeviceWatcher() override;

    /** Start the background mainloop (idempotent). */
    void start();
    /** Quit mainloop and join the worker. */
    void stop();

    /** Thread-safe copy of the last successful enumeration. */
    PulseDeviceLists snapshot() const;

    /**
     * Mark lists dirty so the worker re-enumerates.
     * Called from Pulse callbacks on the worker thread only.
     */
    void requestEnumerate();

signals:
    /** Fired after the cached lists were replaced (use Qt::QueuedConnection). */
    void devicesChanged();

private:
    void threadMain();

    mutable QMutex m_cacheMutex;
    PulseDeviceLists m_cache;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_started{false};
    /** Set by subscribe callback; cleared when an enum pass finishes. */
    std::atomic<bool> m_dirty{false};
    std::atomic<bool> m_enumerating{false};

    QMutex m_loopMutex;
    void *m_ml = nullptr; // pa_mainloop*; worker + stop() only
    QThread *m_thread = nullptr;
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

    /** Latest peak 0..1 (max across channels) for the input meter (lock-free). */
    qreal currentPeak() const {
        const qreal l = m_peakL.load();
        const qreal r = m_peakR.load();
        return l > r ? l : r;
    }
    qreal currentPeakLeft() const { return m_peakL.load(); }
    qreal currentPeakRight() const { return m_peakR.load(); }

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
    std::atomic<qreal> m_peakL{0.0};
    std::atomic<qreal> m_peakR{0.0};

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
    /** Copy of loaded PCM (for edit operations). */
    QByteArray pcm() const;
    QAudioFormat format() const;
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
    /** Peak 0..1 over a short window at @p ms (max of L/R). */
    qreal levelAtPosition(qint64 ms) const;
    /** Per-channel peaks 0..1 over a short window; mono sets both equal. */
    void levelsAtPosition(qint64 ms, qreal *left, qreal *right) const;

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

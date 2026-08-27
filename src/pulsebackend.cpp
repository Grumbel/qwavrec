// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pulsebackend.h"

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include <QMetaObject>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QtMath>
#include <cstring>

// ─── Device enumeration (single connection) ───────────────────────────

namespace {

struct EnumCtx {
    PulseDevices::Lists *lists = nullptr;
    int phase = 0; // 0=server, 1=sources, 2=sinks
};

void contextStateCb(pa_context *c, void *userdata)
{
    auto *ready = static_cast<int *>(userdata);
    const pa_context_state_t st = pa_context_get_state(c);
    if (st == PA_CONTEXT_READY)
        *ready = 1;
    else if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED)
        *ready = -1;
}

void serverInfoCb(pa_context *, const pa_server_info *i, void *userdata)
{
    auto *ctx = static_cast<EnumCtx *>(userdata);
    if (i) {
        if (i->default_source_name)
            ctx->lists->defaultSource = QString::fromUtf8(i->default_source_name);
        if (i->default_sink_name)
            ctx->lists->defaultSink = QString::fromUtf8(i->default_sink_name);
    }
    ctx->phase = 1;
}

void sourceInfoCb(pa_context *, const pa_source_info *i, int eol, void *userdata)
{
    auto *ctx = static_cast<EnumCtx *>(userdata);
    if (eol) {
        ctx->phase = 2;
        return;
    }
    if (!i)
        return;
    PulseDevice d;
    d.name = QString::fromUtf8(i->name);
    d.description = QString::fromUtf8(i->description);
    d.isMonitor = (i->monitor_of_sink != PA_INVALID_INDEX);
    d.isDefault = (d.name == ctx->lists->defaultSource);
    if (d.isMonitor && !d.description.contains(QLatin1String("Monitor"), Qt::CaseInsensitive))
        d.description = QStringLiteral("Monitor of %1").arg(d.description);
    ctx->lists->sources.append(d);
}

void sinkInfoCb(pa_context *, const pa_sink_info *i, int eol, void *userdata)
{
    auto *ctx = static_cast<EnumCtx *>(userdata);
    if (eol) {
        ctx->phase = 3;
        return;
    }
    if (!i)
        return;
    PulseDevice d;
    d.name = QString::fromUtf8(i->name);
    d.description = QString::fromUtf8(i->description);
    d.isDefault = (d.name == ctx->lists->defaultSink);
    ctx->lists->sinks.append(d);
}

} // namespace

PulseDevices::Lists PulseDevices::query()
{
    Lists lists;
    pa_mainloop *ml = pa_mainloop_new();
    if (!ml)
        return lists;
    pa_mainloop_api *api = pa_mainloop_get_api(ml);
    pa_context *ctx = pa_context_new(api, "qwavrec-enum");
    if (!ctx) {
        pa_mainloop_free(ml);
        return lists;
    }

    int ready = 0;
    pa_context_set_state_callback(ctx, contextStateCb, &ready);
    if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return lists;
    }

    // Cap wait so a hung server cannot freeze the app forever
    int spins = 0;
    while (ready == 0 && spins++ < 200) {
        if (pa_mainloop_iterate(ml, 1, nullptr) < 0)
            break;
    }
    if (ready != 1) {
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return lists;
    }

    EnumCtx ectx;
    ectx.lists = &lists;

    pa_operation *op = pa_context_get_server_info(ctx, serverInfoCb, &ectx);
    if (op) {
        while (ectx.phase < 1 && spins++ < 400)
            pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(op);
    }

    op = pa_context_get_source_info_list(ctx, sourceInfoCb, &ectx);
    if (op) {
        while (ectx.phase < 2 && spins++ < 800)
            pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(op);
    }

    op = pa_context_get_sink_info_list(ctx, sinkInfoCb, &ectx);
    if (op) {
        while (ectx.phase < 3 && spins++ < 1200)
            pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(op);
    }

    lists.ok = true;
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return lists;
}

// ─── Capture ──────────────────────────────────────────────────────────

PulseCapture::PulseCapture(QObject *parent)
    : QObject(parent)
{
}

PulseCapture::~PulseCapture()
{
    stop();
}

bool PulseCapture::start(const QString &sourceName, int sampleRate, int channels)
{
    stop();

    m_format = QAudioFormat();
    m_format.setSampleRate(sampleRate);
    m_format.setChannelCount(channels);
    m_format.setSampleFormat(QAudioFormat::Int16);

    {
        QMutexLocker lock(&m_pcmMutex);
        m_pcmQueue.clear();
    }
    m_peak.store(0.0);
    m_stop = false;
    m_running = true;
    const int session = m_session.fetch_add(1) + 1;

    const QString name = sourceName;
    auto *thr = QThread::create([this, name, session]() { runLoop(name, session); });
    thr->setObjectName(QStringLiteral("PulseCapture"));
    thr->setParent(this);
    connect(thr, &QThread::finished, thr, &QObject::deleteLater);
    thr->start();
    return true;
}

void PulseCapture::stop()
{
    m_stop = true;
    m_running = false;
    m_recording.store(false);
    // Session bump so late emits are ignored; worker leaves after current read.
    m_session.fetch_add(1);
    m_peak.store(0.0);
}

void PulseCapture::setRecording(bool on)
{
    m_recording.store(on);
    if (!on) {
        // keep queue for drain
    }
}

QByteArray PulseCapture::takeRecordedAudio()
{
    QMutexLocker lock(&m_pcmMutex);
    QByteArray out;
    out.swap(m_pcmQueue);
    return out;
}

void PulseCapture::runLoop(QString sourceName, int session)
{
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = static_cast<uint32_t>(m_format.sampleRate());
    ss.channels = static_cast<uint8_t>(m_format.channelCount());

    // Small fragments → responsive reads (~20 ms) and quicker stop
    pa_buffer_attr attr;
    memset(&attr, 0xff, sizeof(attr)); // -1u = default
    const uint32_t bytesPer20ms =
        uint32_t(ss.rate / 50) * uint32_t(ss.channels) * 2u;
    attr.fragsize = bytesPer20ms;
    attr.maxlength = bytesPer20ms * 4;

    int error = 0;
    QByteArray devBytes = sourceName.toUtf8();
    const char *dev = sourceName.isEmpty() ? nullptr : devBytes.constData();

    pa_simple *s = pa_simple_new(nullptr, "qwavrec", PA_STREAM_RECORD,
                                 dev, "capture", &ss, nullptr, &attr, &error);
    if (!s) {
        const QString msg = QString::fromUtf8(pa_strerror(error));
        QMetaObject::invokeMethod(this, [this, msg]() {
            m_running = false;
            emit errorOccurred(tr("PulseAudio record failed: %1").arg(msg));
            emit stopped();
        }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(this, [this]() { emit started(); }, Qt::QueuedConnection);

    const int frameBytes = int(ss.channels) * 2;
    const int chunkFrames = int(ss.rate) / 50;
    QByteArray buf(chunkFrames * frameBytes, Qt::Uninitialized);

    while (!m_stop.load() && m_session.load() == session) {
        if (pa_simple_read(s, buf.data(), size_t(buf.size()), &error) < 0)
            break;
        if (m_session.load() != session)
            break;

        auto *samples = reinterpret_cast<qint16 *>(buf.data());
        const int n = buf.size() / 2;
        float peak = 0.f;
        const float g = float(m_gain.load());
        for (int i = 0; i < n; ++i) {
            float v = samples[i] / 32768.f * g;
            v = qBound(-1.f, v, 1.f);
            peak = qMax(peak, qAbs(v));
            samples[i] = qint16(v * 32767.f);
        }
        m_peak.store(qreal(peak));

        if (m_recording.load()) {
            QMutexLocker lock(&m_pcmMutex);
            m_pcmQueue.append(buf);
            // Bound memory if GUI stalls
            const int maxBytes = m_format.sampleRate() * m_format.bytesPerFrame() * 30;
            if (m_pcmQueue.size() > maxBytes)
                m_pcmQueue.remove(0, m_pcmQueue.size() - maxBytes);
        }
    }

    pa_simple_free(s);
    m_running = false;
    m_peak.store(0.0);
    QMetaObject::invokeMethod(this, [this]() { emit stopped(); }, Qt::QueuedConnection);
}

// ─── Playback ─────────────────────────────────────────────────────────

PulsePlayback::PulsePlayback(QObject *parent)
    : QObject(parent)
{
}

PulsePlayback::~PulsePlayback()
{
    stop();
    // Best-effort join so we do not tear down while pa_simple is in use
    const auto threads = findChildren<QThread *>();
    for (QThread *t : threads) {
        if (t->objectName() == QLatin1String("PulsePlayback"))
            t->wait(500);
    }
}

bool PulsePlayback::loadPcm(const QByteArray &pcm, const QAudioFormat &format)
{
    stop();
    // Never emit while holding m_mutex: slots (updateTimeLabel → position())
    // re-enter the same non-recursive mutex and deadlock the GUI thread.
    qint64 durationMs = 0;
    {
        QMutexLocker lock(&m_mutex);
        m_pcm = pcm;
        m_format = format;
        m_byteOffset = 0;
        m_durationMs = 0;
        if (format.sampleRate() > 0 && format.bytesPerFrame() > 0)
            m_durationMs = (pcm.size() / format.bytesPerFrame()) * 1000 / format.sampleRate();
        durationMs = m_durationMs;
    }
    emit durationChanged(durationMs);
    emit positionChanged(0);
    return !pcm.isEmpty();
}

qint64 PulsePlayback::msToBytes(qint64 ms) const
{
    if (m_format.sampleRate() <= 0)
        return 0;
    return m_format.bytesForDuration(ms * 1000);
}

qint64 PulsePlayback::bytesToMs(qint64 bytes) const
{
    if (m_format.sampleRate() <= 0 || m_format.bytesPerFrame() <= 0)
        return 0;
    return (bytes / m_format.bytesPerFrame()) * 1000 / m_format.sampleRate();
}

qint64 PulsePlayback::position() const
{
    QMutexLocker lock(&m_mutex);
    return bytesToMs(m_byteOffset);
}

void PulsePlayback::setSinkName(const QString &name) { m_sinkName = name; }
void PulsePlayback::setVolume(qreal volume) { m_volume = qBound(0.0, volume, 1.0); }

void PulsePlayback::setPlayRange(qint64 startMs, qint64 endMs)
{
    QMutexLocker lock(&m_mutex);
    m_rangeStartMs = qMax(qint64(0), startMs);
    m_rangeEndMs = endMs;
}

void PulsePlayback::clearPlayRange()
{
    QMutexLocker lock(&m_mutex);
    m_rangeStartMs = 0;
    m_rangeEndMs = -1;
}

void PulsePlayback::setPosition(qint64 ms)
{
    // Emit only after releasing the mutex — slots may call position()/levelAtPosition().
    {
        QMutexLocker lock(&m_mutex);
        qint64 lo = m_rangeStartMs;
        qint64 hi = (m_rangeEndMs > m_rangeStartMs) ? m_rangeEndMs : m_durationMs;
        ms = qBound(lo, ms, hi);
        m_byteOffset = msToBytes(ms);
        const int bpf = m_format.bytesPerFrame();
        if (bpf > 0)
            m_byteOffset = (m_byteOffset / bpf) * bpf;
    }
    emit positionChanged(ms);
}

void PulsePlayback::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

void PulsePlayback::play()
{
    if (m_pcm.isEmpty())
        return;
    if (m_state == Playing)
        return;
    if (m_state == Paused) {
        m_pause = false;
        setState(Playing);
        return;
    }

    m_stop = false;
    m_pause = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_rangeEndMs > m_rangeStartMs) {
            const qint64 pos = bytesToMs(m_byteOffset);
            if (pos < m_rangeStartMs || pos >= m_rangeEndMs)
                m_byteOffset = msToBytes(m_rangeStartMs);
        }
    }
    setState(Playing);
    const int gen = m_generation.fetch_add(1) + 1;

    auto *thr = QThread::create([this, gen]() { runLoop(gen); });
    thr->setObjectName(QStringLiteral("PulsePlayback"));
    thr->setParent(this);
    connect(thr, &QThread::finished, thr, &QObject::deleteLater);
    thr->start();
}

void PulsePlayback::pause()
{
    if (m_state != Playing)
        return;
    m_pause = true;
    setState(Paused);
}

void PulsePlayback::stop()
{
    // Non-blocking: never wait on pa_simple_write/drain from the GUI thread.
    m_stop = true;
    m_pause = false;
    m_generation.fetch_add(1);
    {
        QMutexLocker lock(&m_mutex);
        m_byteOffset = 0;
    }
    if (m_state != Stopped) {
        setState(Stopped);
        emit positionChanged(0);
    }
}

void PulsePlayback::runLoop(int generation)
{
    pa_sample_spec ss;
    ss.format = (m_format.sampleFormat() == QAudioFormat::Float)
                    ? PA_SAMPLE_FLOAT32LE
                    : PA_SAMPLE_S16LE;
    ss.rate = static_cast<uint32_t>(m_format.sampleRate());
    ss.channels = static_cast<uint8_t>(m_format.channelCount());

    pa_buffer_attr attr;
    memset(&attr, 0xff, sizeof(attr));
    const uint32_t bytesPer20ms =
        uint32_t(ss.rate / 50) * uint32_t(ss.channels)
        * (ss.format == PA_SAMPLE_FLOAT32LE ? 4u : 2u);
    attr.tlength = bytesPer20ms * 2;
    attr.maxlength = bytesPer20ms * 8;

    int error = 0;
    QByteArray sinkBytes = m_sinkName.toUtf8();
    const char *dev = m_sinkName.isEmpty() ? nullptr : sinkBytes.constData();

    pa_simple *s = pa_simple_new(nullptr, "qwavrec", PA_STREAM_PLAYBACK,
                                 dev, "playback", &ss, nullptr, &attr, &error);
    if (!s) {
        const QString msg = QString::fromUtf8(pa_strerror(error));
        QMetaObject::invokeMethod(this, [this, msg]() {
            emit errorOccurred(tr("PulseAudio playback failed: %1").arg(msg));
            setState(Stopped);
        }, Qt::QueuedConnection);
        return;
    }

    const int bpf = qMax(1, m_format.bytesPerFrame());
    const int chunkBytes = (int(ss.rate) / 50) * bpf;
    qint64 lastPosEmit = -1;

    while (!m_stop.load() && m_generation.load() == generation) {
        while (m_pause.load() && !m_stop.load() && m_generation.load() == generation)
            QThread::msleep(10);
        if (m_stop.load() || m_generation.load() != generation)
            break;

        QByteArray chunk;
        {
            QMutexLocker lock(&m_mutex);
            const qint64 rangeEndBytes = (m_rangeEndMs > m_rangeStartMs)
                ? msToBytes(m_rangeEndMs) : m_pcm.size();
            const qint64 rangeStartBytes = msToBytes(m_rangeStartMs);
            if (m_byteOffset >= rangeEndBytes || m_byteOffset >= m_pcm.size()) {
                if (m_loop)
                    m_byteOffset = rangeStartBytes;
                else
                    break;
            }
            const int n = qMin(chunkBytes,
                               int(qMin(rangeEndBytes, qint64(m_pcm.size())) - m_byteOffset));
            if (n <= 0)
                break;
            chunk = m_pcm.mid(int(m_byteOffset), n);
            m_byteOffset += n;
        }

        if (m_volume < 0.999 && m_format.sampleFormat() == QAudioFormat::Int16) {
            auto *sp = reinterpret_cast<qint16 *>(chunk.data());
            for (int i = 0, ns = chunk.size() / 2; i < ns; ++i)
                sp[i] = qint16(sp[i] * m_volume);
        }

        if (pa_simple_write(s, chunk.constData(), size_t(chunk.size()), &error) < 0)
            break;

        qint64 posMs;
        {
            QMutexLocker lock(&m_mutex);
            posMs = bytesToMs(m_byteOffset);
        }
        // Throttle position signals (~15 Hz). On loop wrap posMs jumps
        // backward — must emit or the seek bar / waveform stay stuck at EOF.
        if (lastPosEmit < 0 || posMs < lastPosEmit || posMs - lastPosEmit >= 60) {
            lastPosEmit = posMs;
            QMetaObject::invokeMethod(this, [this, posMs]() {
                if (m_state == Playing)
                    emit positionChanged(posMs);
            }, Qt::QueuedConnection);
        }
    }

    // drain() blocks until the sink plays out the buffer — skips on abort
    // so Stop/seek/reload cannot freeze the GUI waiting on the worker.
    if (!m_stop.load() && m_generation.load() == generation)
        pa_simple_drain(s, &error);
    pa_simple_free(s);

    if (m_generation.load() == generation) {
        QMetaObject::invokeMethod(this, [this]() {
            if (m_state != Stopped) {
                {
                    QMutexLocker lock(&m_mutex);
                    m_byteOffset = 0;
                }
                setState(Stopped);
                emit positionChanged(0);
            }
        }, Qt::QueuedConnection);
    }
}

qreal PulsePlayback::levelAtPosition(qint64 ms) const
{
    QMutexLocker lock(&m_mutex);
    if (m_pcm.isEmpty() || m_format.bytesPerFrame() <= 0)
        return 0.0;
    const int bpf = m_format.bytesPerFrame();
    qint64 off = msToBytes(ms);
    off = (off / bpf) * bpf;
    const qint64 end = qMin(off + msToBytes(20), qint64(m_pcm.size()));
    if (off >= m_pcm.size())
        return 0.0;
    float peak = 0.f;
    if (m_format.sampleFormat() == QAudioFormat::Int16) {
        const auto *s = reinterpret_cast<const qint16 *>(m_pcm.constData() + off);
        for (int i = 0, n = int((end - off) / 2); i < n; ++i)
            peak = qMax(peak, qAbs(s[i] / 32768.f));
    }
    return qreal(peak);
}

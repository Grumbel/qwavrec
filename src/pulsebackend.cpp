// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pulsebackend.h"

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include <QMetaObject>
#include <QtMath>
#include <cstring>

namespace {

struct ListCtx {
    QVector<PulseDevice> devices;
    QString defaultName;
    bool done = false;
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

void sourceInfoCb(pa_context *, const pa_source_info *i, int eol, void *userdata)
{
    auto *ctx = static_cast<ListCtx *>(userdata);
    if (eol) {
        ctx->done = true;
        return;
    }
    if (!i)
        return;
    PulseDevice d;
    d.name = QString::fromUtf8(i->name);
    d.description = QString::fromUtf8(i->description);
    d.isMonitor = (i->monitor_of_sink != PA_INVALID_INDEX);
    d.isDefault = (d.name == ctx->defaultName);
    if (d.isMonitor && !d.description.contains(QLatin1String("Monitor"), Qt::CaseInsensitive))
        d.description = QStringLiteral("Monitor of %1").arg(d.description);
    ctx->devices.append(d);
}

void sinkInfoCb(pa_context *, const pa_sink_info *i, int eol, void *userdata)
{
    auto *ctx = static_cast<ListCtx *>(userdata);
    if (eol) {
        ctx->done = true;
        return;
    }
    if (!i)
        return;
    PulseDevice d;
    d.name = QString::fromUtf8(i->name);
    d.description = QString::fromUtf8(i->description);
    d.isDefault = (d.name == ctx->defaultName);
    ctx->devices.append(d);
}

template<typename Fn>
bool withContext(Fn fn)
{
    pa_mainloop *ml = pa_mainloop_new();
    if (!ml)
        return false;
    pa_mainloop_api *api = pa_mainloop_get_api(ml);
    pa_context *ctx = pa_context_new(api, "qwavrec");
    if (!ctx) {
        pa_mainloop_free(ml);
        return false;
    }
    int ready = 0;
    pa_context_set_state_callback(ctx, contextStateCb, &ready);
    if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return false;
    }
    while (ready == 0) {
        if (pa_mainloop_iterate(ml, 1, nullptr) < 0)
            break;
    }
    if (ready != 1) {
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return false;
    }
    fn(ctx, ml);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return true;
}

QString getDefaultSource()
{
    QString name;
    withContext([&](pa_context *c, pa_mainloop *ml) {
        struct St { QString *out; bool done; } st{&name, false};
        pa_operation *op = pa_context_get_server_info(c,
            [](pa_context *, const pa_server_info *i, void *userdata) {
                auto *s = static_cast<St *>(userdata);
                if (i && i->default_source_name)
                    *s->out = QString::fromUtf8(i->default_source_name);
                s->done = true;
            }, &st);
        if (!op) return;
        while (!st.done)
            pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(op);
    });
    return name;
}

QString getDefaultSink()
{
    QString name;
    withContext([&](pa_context *c, pa_mainloop *ml) {
        struct St { QString *out; bool done; } st{&name, false};
        pa_operation *op = pa_context_get_server_info(c,
            [](pa_context *, const pa_server_info *i, void *userdata) {
                auto *s = static_cast<St *>(userdata);
                if (i && i->default_sink_name)
                    *s->out = QString::fromUtf8(i->default_sink_name);
                s->done = true;
            }, &st);
        if (!op) return;
        while (!st.done)
            pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(op);
    });
    return name;
}

} // namespace

QVector<PulseDevice> PulseDevices::sources()
{
    ListCtx list;
    list.defaultName = getDefaultSource();
    withContext([&](pa_context *c, pa_mainloop *ml) {
        list.done = false;
        pa_operation *op = pa_context_get_source_info_list(c, sourceInfoCb, &list);
        if (!op) return;
        while (!list.done)
            pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(op);
    });
    return list.devices;
}

QVector<PulseDevice> PulseDevices::sinks()
{
    ListCtx list;
    list.defaultName = getDefaultSink();
    withContext([&](pa_context *c, pa_mainloop *ml) {
        list.done = false;
        pa_operation *op = pa_context_get_sink_info_list(c, sinkInfoCb, &list);
        if (!op) return;
        while (!list.done)
            pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(op);
    });
    return list.devices;
}

QString PulseDevices::defaultSourceName() { return getDefaultSource(); }
QString PulseDevices::defaultSinkName() { return getDefaultSink(); }

// ---- Capture ----

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
    m_stop = false;
    m_running = true;

    const QString name = sourceName;
    auto *thr = QThread::create([this, name]() { runLoop(name); });
    connect(thr, &QThread::finished, thr, &QObject::deleteLater);
    // Track via m_thread is awkward with QThread::create; store pointer
    thr->setObjectName(QStringLiteral("PulseCapture"));
    thr->start();
    // Keep reference for wait on stop: use QPointer pattern via property
    thr->setParent(this); // will be deleteLater on finish — parented so stop can find?
    return true;
}

void PulseCapture::stop()
{
    m_stop = true;
    // Wait for capture threads we parented
    const auto threads = findChildren<QThread *>();
    for (QThread *t : threads) {
        if (t->objectName() == QLatin1String("PulseCapture"))
            t->wait(3000);
    }
    m_running = false;
}

void PulseCapture::runLoop(QString sourceName)
{
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = static_cast<uint32_t>(m_format.sampleRate());
    ss.channels = static_cast<uint8_t>(m_format.channelCount());

    int error = 0;
    QByteArray devBytes = sourceName.toUtf8();
    const char *dev = sourceName.isEmpty() ? nullptr : devBytes.constData();

    pa_simple *s = pa_simple_new(nullptr, "qwavrec", PA_STREAM_RECORD,
                                 dev, "capture", &ss, nullptr, nullptr, &error);
    if (!s) {
        const QString msg = QString::fromUtf8(pa_strerror(error));
        QMetaObject::invokeMethod(this, [this, msg]() {
            m_running = false;
            emit errorOccurred(tr("PulseAudio record failed: %1").arg(msg));
            emit stopped();
        }, Qt::QueuedConnection);
        return;
    }

    const int frameBytes = int(ss.channels) * 2;
    const int chunkFrames = int(ss.rate) / 50;
    QByteArray buf(chunkFrames * frameBytes, Qt::Uninitialized);

    while (!m_stop.load()) {
        if (pa_simple_read(s, buf.data(), size_t(buf.size()), &error) < 0)
            break;

        auto *samples = reinterpret_cast<qint16 *>(buf.data());
        const int n = buf.size() / 2;
        float peak = 0.f;
        const float g = float(m_gain);
        for (int i = 0; i < n; ++i) {
            float v = samples[i] / 32768.f * g;
            v = qBound(-1.f, v, 1.f);
            peak = qMax(peak, qAbs(v));
            samples[i] = qint16(v * 32767.f);
        }

        const QByteArray copy = buf;
        QMetaObject::invokeMethod(this, [this, copy, peak]() {
            emit samplesReady(copy, peak);
        }, Qt::QueuedConnection);
    }

    pa_simple_free(s);
    m_running = false;
    QMetaObject::invokeMethod(this, [this]() { emit stopped(); }, Qt::QueuedConnection);
}

// ---- Playback ----

PulsePlayback::PulsePlayback(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<PulsePlayback::State>("PulsePlayback::State");
}
// ctor cont

{
}

PulsePlayback::~PulsePlayback()
{
    stop();
    const auto threads = findChildren<QThread *>();
    for (QThread *t : threads) {
        if (t->objectName() == QLatin1String("PulsePlayback"))
            t->wait(3000);
    }
}

bool PulsePlayback::loadPcm(const QByteArray &pcm, const QAudioFormat &format)
{
    stop();
    QMutexLocker lock(&m_mutex);
    m_pcm = pcm;
    m_format = format;
    m_byteOffset = 0;
    m_durationMs = 0;
    if (format.sampleRate() > 0 && format.bytesPerFrame() > 0)
        m_durationMs = (pcm.size() / format.bytesPerFrame()) * 1000 / format.sampleRate();
    emit durationChanged(m_durationMs);
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

void PulsePlayback::setPosition(qint64 ms)
{
    QMutexLocker lock(&m_mutex);
    ms = qBound(qint64(0), ms, m_durationMs);
    m_byteOffset = msToBytes(ms);
    const int bpf = m_format.bytesPerFrame();
    if (bpf > 0)
        m_byteOffset = (m_byteOffset / bpf) * bpf;
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
    setState(Playing);

    auto *thr = QThread::create([this]() { runLoop(); });
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
    m_stop = true;
    m_pause = false;
    const auto threads = findChildren<QThread *>();
    for (QThread *t : threads) {
        if (t->objectName() == QLatin1String("PulsePlayback"))
            t->wait(3000);
    }
    {
        QMutexLocker lock(&m_mutex);
        m_byteOffset = 0;
    }
    if (m_state != Stopped) {
        setState(Stopped);
        emit positionChanged(0);
    }
}

void PulsePlayback::runLoop()
{
    pa_sample_spec ss;
    ss.format = (m_format.sampleFormat() == QAudioFormat::Float)
                    ? PA_SAMPLE_FLOAT32LE
                    : PA_SAMPLE_S16LE;
    ss.rate = static_cast<uint32_t>(m_format.sampleRate());
    ss.channels = static_cast<uint8_t>(m_format.channelCount());

    int error = 0;
    QByteArray sinkBytes = m_sinkName.toUtf8();
    const char *dev = m_sinkName.isEmpty() ? nullptr : sinkBytes.constData();

    pa_simple *s = pa_simple_new(nullptr, "qwavrec", PA_STREAM_PLAYBACK,
                                 dev, "playback", &ss, nullptr, nullptr, &error);
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

    while (!m_stop.load()) {
        while (m_pause.load() && !m_stop.load())
            QThread::msleep(10);
        if (m_stop.load())
            break;

        QByteArray chunk;
        {
            QMutexLocker lock(&m_mutex);
            if (m_byteOffset >= m_pcm.size()) {
                if (m_loop)
                    m_byteOffset = 0;
                else
                    break;
            }
            const int n = qMin(chunkBytes, int(m_pcm.size() - m_byteOffset));
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
        QMetaObject::invokeMethod(this, [this, posMs]() {
            if (m_state == Playing)
                emit positionChanged(posMs);
        }, Qt::QueuedConnection);
    }

    pa_simple_drain(s, &error);
    pa_simple_free(s);

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

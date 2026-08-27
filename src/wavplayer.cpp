// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wavplayer.h"

#include <QtEndian>
#include <QFile>
#include <QMediaDevices>
#include <QtMath>

WavPlayer::WavPlayer(QObject *parent)
    : QObject(parent)
{
    m_device = QMediaDevices::defaultAudioOutput();
    connect(&m_posTimer, &QTimer::timeout, this, &WavPlayer::tickPosition);
    m_posTimer.setInterval(40);
}

WavPlayer::~WavPlayer()
{
    stop();
    delete m_sink;
    m_sink = nullptr;
}

bool WavPlayer::load(const QString &path)
{
    stop();
    m_pcm.clear();
    m_path.clear();
    m_durationMs = 0;
    m_byteOffset = 0;

    if (!parseAndLoad(path))
        return false;

    m_path = path;
    if (m_format.sampleRate() > 0 && m_format.bytesPerFrame() > 0) {
        const qint64 frames = m_pcm.size() / m_format.bytesPerFrame();
        m_durationMs = frames * 1000 / m_format.sampleRate();
    }
    emit durationChanged(m_durationMs);
    emit positionChanged(0);
    return true;
}

bool WavPlayer::parseAndLoad(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Could not open file:\n%1").arg(path));
        return false;
    }

    const QByteArray riff = file.read(12);
    if (riff.size() < 12 || !riff.startsWith("RIFF") || riff.mid(8, 4) != "WAVE") {
        emit errorOccurred(tr("Not a RIFF/WAVE file:\n%1").arg(path));
        return false;
    }

    bool gotFmt = false;
    qint64 dataOffset = -1;
    quint32 dataSize = 0;

    while (!file.atEnd()) {
        const QByteArray id = file.read(4);
        const QByteArray szb = file.read(4);
        if (id.size() < 4 || szb.size() < 4)
            break;
        const quint32 sz = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(szb.constData()));
        const qint64 chunkPos = file.pos();

        if (id == "fmt ") {
            QByteArray fmt = file.read(qMin(sz, quint32(64)));
            if (fmt.size() < 16) {
                emit errorOccurred(tr("Invalid fmt chunk"));
                return false;
            }
            const quint16 audioFormat = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmt.constData()));
            const quint16 channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmt.constData() + 2));
            const quint32 sampleRate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(fmt.constData() + 4));
            const quint16 bitsPerSample = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmt.constData() + 14));

            m_format = QAudioFormat();
            m_format.setChannelCount(channels);
            m_format.setSampleRate(int(sampleRate));
            if (audioFormat == 1) {
                if (bitsPerSample == 8)
                    m_format.setSampleFormat(QAudioFormat::UInt8);
                else if (bitsPerSample == 16)
                    m_format.setSampleFormat(QAudioFormat::Int16);
                else if (bitsPerSample == 32)
                    m_format.setSampleFormat(QAudioFormat::Int32);
                else {
                    emit errorOccurred(tr("Unsupported PCM bit depth: %1").arg(bitsPerSample));
                    return false;
                }
            } else if (audioFormat == 3 && bitsPerSample == 32) {
                m_format.setSampleFormat(QAudioFormat::Float);
            } else {
                emit errorOccurred(tr("Unsupported WAV format code: %1").arg(audioFormat));
                return false;
            }
            gotFmt = true;
            if (int(sz) > fmt.size())
                file.seek(chunkPos + sz + (sz & 1));
        } else if (id == "data") {
            dataOffset = chunkPos;
            dataSize = sz;
            break;
        } else {
            file.seek(chunkPos + sz + (sz & 1));
        }
    }

    if (!gotFmt || dataOffset < 0 || dataSize == 0) {
        emit errorOccurred(tr("WAV missing fmt or data chunk"));
        return false;
    }

    file.seek(dataOffset);
    m_pcm = file.read(dataSize);
    if (m_pcm.isEmpty()) {
        emit errorOccurred(tr("Could not read WAV data"));
        return false;
    }
    return true;
}

void WavPlayer::ensureSink()
{
    if (m_sink)
        return;
    if (m_device.isNull())
        m_device = QMediaDevices::defaultAudioOutput();
    m_sink = new QAudioSink(m_device, m_format, this);
    m_sink->setVolume(m_volume);
    connect(m_sink, &QAudioSink::stateChanged, this, &WavPlayer::onSinkStateChanged);
}

void WavPlayer::stopSink()
{
    if (!m_sink)
        return;
    m_restarting = true;
    m_sink->stop();
    m_restarting = false;
}

void WavPlayer::startFromByteOffset(qint64 byteOffset)
{
    ensureSink();
    if (!m_sink)
        return;

    const int bpf = m_format.bytesPerFrame();
    if (bpf > 0)
        byteOffset = (byteOffset / bpf) * bpf;
    byteOffset = qBound(qint64(0), byteOffset, qint64(m_pcm.size()));
    m_byteOffset = byteOffset;

    stopSink();

    m_buffer.close();
    m_buffer.setData(QByteArray::fromRawData(m_pcm.constData() + byteOffset,
                                             int(m_pcm.size() - byteOffset)));
    // fromRawData does not copy — keep m_pcm alive; QBuffer needs owned data for safety:
    m_buffer.setData(m_pcm.mid(int(byteOffset)));
    m_buffer.open(QIODevice::ReadOnly);

    m_sink->start(&m_buffer);
}

qint64 WavPlayer::msToBytes(qint64 ms) const
{
    if (m_format.sampleRate() <= 0)
        return 0;
    return m_format.bytesForDuration(ms * 1000);
}

qint64 WavPlayer::bytesToMs(qint64 bytes) const
{
    if (m_format.sampleRate() <= 0 || m_format.bytesPerFrame() <= 0)
        return 0;
    return (bytes / m_format.bytesPerFrame()) * 1000 / m_format.sampleRate();
}

void WavPlayer::play()
{
    if (m_pcm.isEmpty())
        return;

    if (m_state == Paused && m_sink && m_sink->state() == QAudio::SuspendedState) {
        m_sink->resume();
        setState(Playing);
        m_posTimer.start();
        return;
    }

    // Start (or restart) from current byte offset
    startFromByteOffset(m_byteOffset);
    setState(Playing);
    m_posTimer.start();
}

void WavPlayer::pause()
{
    if (m_state != Playing || !m_sink)
        return;
    // Capture approximate position before suspend
    m_byteOffset = msToBytes(position());
    m_sink->suspend();
    setState(Paused);
    m_posTimer.stop();
    emit positionChanged(position());
}

void WavPlayer::stop()
{
    m_posTimer.stop();
    stopSink();
    m_buffer.close();
    m_byteOffset = 0;
    setState(Stopped);
    emit positionChanged(0);
}

void WavPlayer::setPosition(qint64 ms)
{
    if (m_pcm.isEmpty())
        return;
    ms = qBound(qint64(0), ms, m_durationMs);
    const qint64 bytes = msToBytes(ms);

    if (m_state == Playing) {
        startFromByteOffset(bytes);
        setState(Playing);
        m_posTimer.start();
    } else {
        m_byteOffset = bytes;
        // Stay Stopped or Paused; next play continues from here
        if (m_state == Paused)
            setState(Paused);
    }
    emit positionChanged(ms);
}

void WavPlayer::setVolume(qreal volume)
{
    m_volume = qBound(0.0, volume, 1.0);
    if (m_sink)
        m_sink->setVolume(m_volume);
}

void WavPlayer::setLoop(bool loop)
{
    m_loop = loop;
}

void WavPlayer::setDevice(const QAudioDevice &device)
{
    const bool wasPlaying = (m_state == Playing);
    const qint64 pos = position();
    stop();
    if (m_sink) {
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    m_device = device;
    m_byteOffset = msToBytes(pos);
    if (wasPlaying)
        play();
}

qint64 WavPlayer::position() const
{
    if (m_pcm.isEmpty())
        return 0;
    if (m_state == Playing && m_sink) {
        // elapsedUSecs is relative to last start()
        const qint64 elapsedMs = m_sink->elapsedUSecs() / 1000;
        return qMin(m_durationMs, bytesToMs(m_byteOffset) + elapsedMs);
    }
    return bytesToMs(m_byteOffset);
}

qreal WavPlayer::levelAtPosition(qint64 ms) const
{
    if (m_pcm.isEmpty() || m_format.bytesPerFrame() <= 0)
        return 0.0;
    const int bpf = m_format.bytesPerFrame();
    qint64 off = msToBytes(ms);
    off = (off / bpf) * bpf;
    // ~20 ms window
    const qint64 win = msToBytes(20);
    if (off >= m_pcm.size())
        return 0.0;
    const qint64 end = qMin(off + win, qint64(m_pcm.size()));
    float peak = 0.f;
    if (m_format.sampleFormat() == QAudioFormat::Int16) {
        const auto *s = reinterpret_cast<const qint16 *>(m_pcm.constData() + off);
        const int n = int((end - off) / sizeof(qint16));
        for (int i = 0; i < n; ++i)
            peak = qMax(peak, qAbs(s[i] / 32768.f));
    } else if (m_format.sampleFormat() == QAudioFormat::Float) {
        const auto *s = reinterpret_cast<const float *>(m_pcm.constData() + off);
        const int n = int((end - off) / sizeof(float));
        for (int i = 0; i < n; ++i)
            peak = qMax(peak, qAbs(s[i]));
    }
    return qreal(peak);
}

void WavPlayer::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

void WavPlayer::onSinkStateChanged(QAudio::State state)
{
    if (m_restarting)
        return;

    if (state == QAudio::IdleState && m_state == Playing) {
        if (m_loop && !m_pcm.isEmpty()) {
            startFromByteOffset(0);
            return;
        }
        m_posTimer.stop();
        m_byteOffset = 0;
        setState(Stopped);
        emit positionChanged(0);
    } else if (state == QAudio::StoppedState) {
        if (m_sink && m_sink->error() != QAudio::NoError && m_state == Playing) {
            emit errorOccurred(tr("Audio output error"));
            m_posTimer.stop();
            setState(Stopped);
        }
    }
}

void WavPlayer::tickPosition()
{
    if (m_state == Playing)
        emit positionChanged(position());
}

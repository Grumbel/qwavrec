// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wavplayer.h"

#include <QtEndian>
#include <QFile>
#include <QMediaDevices>

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
    m_pauseOffset = 0;

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
            if (sz > 64)
                file.seek(chunkPos + sz + (sz & 1));
            else if (fmt.size() < int(sz))
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
    if (m_pcm.size() < int(dataSize) && m_pcm.isEmpty()) {
        emit errorOccurred(tr("Could not read WAV data"));
        return false;
    }
    return true;
}

void WavPlayer::recreateSink()
{
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_device.isNull())
        m_device = QMediaDevices::defaultAudioOutput();

    if (!m_device.isFormatSupported(m_format)) {
        // Still try — some backends accept and convert
    }

    m_sink = new QAudioSink(m_device, m_format, this);
    m_sink->setVolume(m_volume);
    connect(m_sink, &QAudioSink::stateChanged, this, &WavPlayer::onSinkStateChanged);
}

void WavPlayer::play()
{
    if (m_pcm.isEmpty())
        return;

    if (m_state == Paused && m_sink) {
        m_sink->resume();
        setState(Playing);
        m_posTimer.start();
        return;
    }

    recreateSink();
    if (!m_sink)
        return;

    m_buffer.close();
    m_buffer.setData(m_pcm);
    m_buffer.open(QIODevice::ReadOnly);
    if (m_pauseOffset > 0 && m_pauseOffset < m_pcm.size())
        m_buffer.seek(m_pauseOffset);
    else
        m_pauseOffset = 0;

    m_sink->start(&m_buffer);
    setState(Playing);
    m_posTimer.start();
}

void WavPlayer::pause()
{
    if (m_state != Playing || !m_sink)
        return;
    m_pauseOffset = m_buffer.pos();
    m_sink->suspend();
    setState(Paused);
    m_posTimer.stop();
    emit positionChanged(position());
}

void WavPlayer::stop()
{
    m_posTimer.stop();
    if (m_sink) {
        m_sink->stop();
    }
    m_buffer.close();
    m_pauseOffset = 0;
    setState(Stopped);
    emit positionChanged(0);
}

void WavPlayer::setPosition(qint64 ms)
{
    if (m_pcm.isEmpty() || m_format.sampleRate() <= 0)
        return;
    const qint64 bytes = m_format.bytesForDuration(ms * 1000);
    m_pauseOffset = qBound(qint64(0), bytes, qint64(m_pcm.size()));
    // Align to frame
    const int bpf = m_format.bytesPerFrame();
    if (bpf > 0)
        m_pauseOffset = (m_pauseOffset / bpf) * bpf;

    if (m_state == Playing) {
        stop();
        play();
    } else {
        emit positionChanged(position());
    }
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
    if (m_state != Stopped)
        stop();
    m_device = device;
    if (wasPlaying) {
        setPosition(pos);
        play();
    }
}

qint64 WavPlayer::position() const
{
    if (m_pcm.isEmpty() || m_format.sampleRate() <= 0)
        return 0;
    qint64 bytes = m_pauseOffset;
    if (m_state == Playing && m_buffer.isOpen())
        bytes = m_buffer.pos();
    const int bpf = m_format.bytesPerFrame();
    if (bpf <= 0)
        return 0;
    const qint64 frames = bytes / bpf;
    return frames * 1000 / m_format.sampleRate();
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
    if (state == QAudio::IdleState && m_state == Playing) {
        if (m_loop && !m_pcm.isEmpty()) {
            m_pauseOffset = 0;
            m_buffer.close();
            m_buffer.setData(m_pcm);
            m_buffer.open(QIODevice::ReadOnly);
            m_sink->start(&m_buffer);
            emit positionChanged(0);
            return;
        }
        m_posTimer.stop();
        m_pauseOffset = 0;
        setState(Stopped);
        emit positionChanged(0);
    } else if (state == QAudio::StoppedState) {
        if (m_sink && m_sink->error() != QAudio::NoError) {
            emit errorOccurred(tr("Audio output error"));
            setState(Stopped);
        }
    }
}

void WavPlayer::tickPosition()
{
    if (m_state == Playing)
        emit positionChanged(position());
}

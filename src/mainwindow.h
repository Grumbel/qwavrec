// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioSource>
#include <QAudioBufferOutput>
#include <QAudioDecoder>
#include <QIODevice>
#include <QVector>
#include <QElapsedTimer>

#include "wavwriter.h"

class QComboBox;
class QLabel;
class QSlider;
class QAction;
class LevelMeter;
class WaveformWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateAudioDevices();
    void onInputDeviceChanged(int index);
    void onOutputDeviceChanged(int index);
    void onInputVolumeChanged(int value);
    void onOutputVolumeChanged(int value);

    void onNew();
    void onOpen();
    void onSave();
    void onSaveAs();
    void onRecord();
    void onPlay();
    void onAbout();

    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onPlayerError(QMediaPlayer::Error error, const QString &errorString);
    void onSeek(int value);

    void onAudioSourceReadyRead();
    void onAudioBufferReceived(const QAudioBuffer &buffer);
    void onDecoderBufferReady();
    void onDecoderFinished();

private:
    enum class AppState { Ready, Playing, Paused, Recording, Error };

    void createActions();
    void createMenus();
    void createToolBar();
    void createCentralWidget();
    void setAppState(AppState state);
    void updateControls();
    void updateTimeLabel();
    void updateWindowTitle();
    QString formatTime(qint64 ms) const;

    void startAudioSource();
    void stopAudioSource();
    qreal computeLevel(const QAudioBuffer &buffer) const;

    bool maybeSave();
    void clearDocument();
    void setDocumentPath(const QString &path, bool isTemporary);
    QString documentPathForPlayback() const;
    bool hasDocument() const;
    void markModified();
    void loadWaveformFromDocument();
    void stopDecoder();

    // Apply mic gain (0..2) to Int16 buffer in-place; returns peak 0..1
    float processCaptureBuffer(QByteArray &data, const QAudioFormat &fmt);

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_recordAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_aboutAction = nullptr;

    QLabel *m_fileLabel = nullptr;
    QComboBox *m_inputCombo = nullptr;
    QComboBox *m_outputCombo = nullptr;
    QSlider *m_inputVolumeSlider = nullptr;
    QSlider *m_outputVolumeSlider = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    LevelMeter *m_inputMeter = nullptr;
    LevelMeter *m_outputMeter = nullptr;
    WaveformWidget *m_waveform = nullptr;

    QMediaDevices m_devices;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QAudioBufferOutput *m_bufferOutput = nullptr;

    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_audioSourceDevice = nullptr;
    WavWriter m_wavWriter;
    qreal m_micGain = 1.0; // 0..2

    QAudioDecoder *m_decoder = nullptr;
    QVector<float> m_decodePeaks;
    QVector<float> m_liveRecordPeaks; // built while recording

    QString m_savedPath;
    QString m_tempPath;
    bool m_isTemporary = true;
    bool m_modified = false;

    AppState m_state = AppState::Ready;
    qint64 m_duration = 0;
    bool m_seeking = false;
    QElapsedTimer m_recordTimer;
};

#endif

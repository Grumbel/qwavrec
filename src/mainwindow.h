// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QMediaCaptureSession>
#include <QAudioInput>
#include <QMediaRecorder>
#include <QAudioSource>
#include <QAudioBufferOutput>
#include <QIODevice>

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

private slots:
    void updateAudioDevices();
    void onInputDeviceChanged(int index);
    void onOutputDeviceChanged(int index);
    void onOpen();
    void onSaveAs();
    void onRecord();
    void onPlay();
    void onStop();
    void onAbout();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlayerError(QMediaPlayer::Error error, const QString &errorString);
    void onRecorderError(QMediaRecorder::Error error, const QString &errorString);
    void onSeek(int value);
    void onAudioSourceReadyRead();
    void onAudioBufferReceived(const QAudioBuffer &buffer);

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
    void startInputMonitoring();
    void stopInputMonitoring();
    qreal computeLevel(const QAudioBuffer &buffer) const;

    QAction *m_openAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_recordAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_aboutAction = nullptr;

    QLabel *m_fileLabel = nullptr;
    QComboBox *m_inputCombo = nullptr;
    QComboBox *m_outputCombo = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    LevelMeter *m_inputMeter = nullptr;
    LevelMeter *m_outputMeter = nullptr;
    WaveformWidget *m_waveform = nullptr;

    QMediaDevices m_devices;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QAudioBufferOutput *m_bufferOutput = nullptr;
    QMediaCaptureSession *m_captureSession = nullptr;
    QAudioInput *m_audioInput = nullptr;
    QMediaRecorder *m_recorder = nullptr;
    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_audioSourceDevice = nullptr;

    AppState m_state = AppState::Ready;
    qint64 m_duration = 0;
    bool m_seeking = false;
    QString m_currentFile;
};

#endif

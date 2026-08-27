// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QMediaCaptureSession>
#include <QAudioInput>
#include <QMediaRecorder>
#include <QUrl>

class QComboBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QLabel;

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
    void onBrowseFile();
    void onRecord();
    void onPlay();
    void onStop();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlayerError(QMediaPlayer::Error error, const QString &errorString);
    void onRecorderError(QMediaRecorder::Error error, const QString &errorString);
    void onSeek(int value);

private:
    enum class AppState {
        Ready,
        Playing,
        Paused,
        Recording,
        Error
    };

    void setAppState(AppState state);
    void updateControls();
    void updateTimeLabel();
    QString formatTime(qint64 ms) const;

    QLineEdit *m_fileEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QComboBox *m_inputCombo = nullptr;
    QComboBox *m_outputCombo = nullptr;
    QPushButton *m_recordButton = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;

    QMediaDevices m_devices;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QMediaCaptureSession *m_captureSession = nullptr;
    QAudioInput *m_audioInput = nullptr;
    QMediaRecorder *m_recorder = nullptr;

    AppState m_state = AppState::Ready;
    qint64 m_duration = 0;
    bool m_seeking = false;
};

#endif // MAINWINDOW_H

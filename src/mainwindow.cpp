// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QMediaFormat>
#include <QAudioDevice>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Simple Audio"));
    setMinimumWidth(420);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);

    // File
    auto *fileLayout = new QHBoxLayout;
    m_fileEdit = new QLineEdit;
    m_fileEdit->setPlaceholderText(tr("Select or enter a file path…"));
    m_browseButton = new QPushButton(tr("…"));
    m_browseButton->setFixedWidth(40);
    fileLayout->addWidget(m_fileEdit);
    fileLayout->addWidget(m_browseButton);
    mainLayout->addLayout(fileLayout);

    // Devices
    auto *deviceForm = new QFormLayout;
    m_inputCombo = new QComboBox;
    m_outputCombo = new QComboBox;
    deviceForm->addRow(tr("Input"), m_inputCombo);
    deviceForm->addRow(tr("Output"), m_outputCombo);
    mainLayout->addLayout(deviceForm);

    // Buttons
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    m_recordButton = new QPushButton(tr("● Record"));
    m_playButton = new QPushButton(tr("▶ Play"));
    m_stopButton = new QPushButton(tr("■ Stop"));
    buttonLayout->addWidget(m_recordButton);
    buttonLayout->addWidget(m_playButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // Seek / time
    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    mainLayout->addWidget(m_seekSlider);

    m_timeLabel = new QLabel(tr("00:00 / 00:00"));
    m_timeLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_timeLabel);

    m_statusLabel = new QLabel(tr("Status: Ready"));
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();

    // Media objects
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    m_captureSession = new QMediaCaptureSession(this);
    m_audioInput = new QAudioInput(this);
    m_captureSession->setAudioInput(m_audioInput);
    m_recorder = new QMediaRecorder(this);
    m_captureSession->setRecorder(m_recorder);

    // Connections
    connect(&m_devices, &QMediaDevices::audioInputsChanged, this, &MainWindow::updateAudioDevices);
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, &MainWindow::updateAudioDevices);

    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputDeviceChanged);
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);

    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseFile);
    connect(m_recordButton, &QPushButton::clicked, this, &MainWindow::onRecord);
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::onPlay);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);

    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::onPlayerStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MainWindow::onPlayerError);

    connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(m_recorder, &QMediaRecorder::durationChanged, this, [this](qint64 duration) {
        if (m_state == AppState::Recording) {
            m_timeLabel->setText(formatTime(duration));
        }
    });
    connect(m_recorder, &QMediaRecorder::errorOccurred, this, &MainWindow::onRecorderError);

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_seeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        onSeek(m_seekSlider->value());
    });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (m_seeking) {
            m_timeLabel->setText(tr("%1 / %2").arg(formatTime(value), formatTime(m_duration)));
        }
    });

    updateAudioDevices();
    setAppState(AppState::Ready);
}

MainWindow::~MainWindow()
{
    if (m_state == AppState::Recording) {
        m_recorder->stop();
    }
    if (m_state == AppState::Playing || m_state == AppState::Paused) {
        m_player->stop();
    }
}

void MainWindow::updateAudioDevices()
{
    QByteArray currentInputId;
    QByteArray currentOutputId;
    if (m_inputCombo->currentIndex() >= 0) {
        currentInputId = m_inputCombo->currentData().toByteArray();
    }
    if (m_outputCombo->currentIndex() >= 0) {
        currentOutputId = m_outputCombo->currentData().toByteArray();
    }

    m_inputCombo->blockSignals(true);
    m_outputCombo->blockSignals(true);

    m_inputCombo->clear();
    const auto inputs = QMediaDevices::audioInputs();
    int inputIndex = 0;
    int selectedInput = 0;
    for (const QAudioDevice &dev : inputs) {
        m_inputCombo->addItem(dev.description(), dev.id());
        if (dev.id() == currentInputId) {
            selectedInput = inputIndex;
        } else if (currentInputId.isEmpty() && dev.isDefault()) {
            selectedInput = inputIndex;
        }
        ++inputIndex;
    }
    if (m_inputCombo->count() > 0) {
        m_inputCombo->setCurrentIndex(selectedInput);
        onInputDeviceChanged(selectedInput);
    }

    m_outputCombo->clear();
    const auto outputs = QMediaDevices::audioOutputs();
    int outputIndex = 0;
    int selectedOutput = 0;
    for (const QAudioDevice &dev : outputs) {
        m_outputCombo->addItem(dev.description(), dev.id());
        if (dev.id() == currentOutputId) {
            selectedOutput = outputIndex;
        } else if (currentOutputId.isEmpty() && dev.isDefault()) {
            selectedOutput = outputIndex;
        }
        ++outputIndex;
    }
    if (m_outputCombo->count() > 0) {
        m_outputCombo->setCurrentIndex(selectedOutput);
        onOutputDeviceChanged(selectedOutput);
    }

    m_inputCombo->blockSignals(false);
    m_outputCombo->blockSignals(false);
}

void MainWindow::onInputDeviceChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QByteArray id = m_inputCombo->itemData(index).toByteArray();
    const auto inputs = QMediaDevices::audioInputs();
    for (const QAudioDevice &dev : inputs) {
        if (dev.id() == id) {
            m_audioInput->setDevice(dev);
            if (m_state == AppState::Recording) {
                m_recorder->stop();
            }
            break;
        }
    }
}

void MainWindow::onOutputDeviceChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QByteArray id = m_outputCombo->itemData(index).toByteArray();
    const auto outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &dev : outputs) {
        if (dev.id() == id) {
            m_audioOutput->setDevice(dev);
            break;
        }
    }
}

void MainWindow::onBrowseFile()
{
    QString path;
    if (m_state == AppState::Recording || m_fileEdit->text().isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this,
            tr("Select Recording File"),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/recording.wav",
            tr("WAV Audio (*.wav);;All Files (*)"));
    } else {
        path = QFileDialog::getOpenFileName(
            this,
            tr("Open Audio File"),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            tr("Audio Files (*.wav *.flac *.ogg *.mp3 *.m4a);;All Files (*)"));
    }
    if (!path.isEmpty()) {
        m_fileEdit->setText(path);
    }
}

void MainWindow::onRecord()
{
    if (m_state == AppState::Recording) {
        return;
    }
    if (m_state == AppState::Playing || m_state == AppState::Paused) {
        m_player->stop();
    }

    QString path = m_fileEdit->text().trimmed();
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/recording.wav";
        m_fileEdit->setText(path);
    }

    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    m_recorder->setOutputLocation(QUrl::fromLocalFile(path));

    QMediaFormat format;
    format.setFileFormat(QMediaFormat::Wave);
    format.setAudioCodec(QMediaFormat::AudioCodec::Wave);
    m_recorder->setMediaFormat(format);
    m_recorder->setQuality(QMediaRecorder::HighQuality);

    m_recorder->record();
}

void MainWindow::onPlay()
{
    if (m_state == AppState::Recording) {
        return;
    }

    const QString path = m_fileEdit->text().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Error"), tr("Please select an existing audio file to play."));
        return;
    }

    if (m_state == AppState::Paused) {
        m_player->play();
        setAppState(AppState::Playing);
        return;
    }

    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
}

void MainWindow::onStop()
{
    if (m_state == AppState::Recording) {
        m_recorder->stop();
    } else if (m_state == AppState::Playing || m_state == AppState::Paused) {
        m_player->stop();
    }
}

void MainWindow::onPositionChanged(qint64 position)
{
    if (!m_seeking && m_state == AppState::Playing) {
        m_seekSlider->setValue(static_cast<int>(position));
        updateTimeLabel();
    }
}

void MainWindow::onDurationChanged(qint64 duration)
{
    m_duration = duration;
    m_seekSlider->setRange(0, static_cast<int>(duration));
    updateTimeLabel();
}

void MainWindow::onPlayerStateChanged(QMediaPlayer::PlaybackState state)
{
    switch (state) {
    case QMediaPlayer::PlayingState:
        setAppState(AppState::Playing);
        break;
    case QMediaPlayer::PausedState:
        setAppState(AppState::Paused);
        break;
    case QMediaPlayer::StoppedState:
        if (m_state != AppState::Recording) {
            setAppState(AppState::Ready);
            m_seekSlider->setValue(0);
            updateTimeLabel();
        }
        break;
    }
}

void MainWindow::onRecorderStateChanged(QMediaRecorder::RecorderState state)
{
    switch (state) {
    case QMediaRecorder::RecordingState:
        setAppState(AppState::Recording);
        break;
    case QMediaRecorder::PausedState:
        break;
    case QMediaRecorder::StoppedState:
        if (m_state == AppState::Recording) {
            setAppState(AppState::Ready);
        }
        break;
    }
}

void MainWindow::onPlayerError(QMediaPlayer::Error error, const QString &errorString)
{
    Q_UNUSED(error);
    QMessageBox::critical(this, tr("Playback Error"),
                          tr("Could not play file:\n%1").arg(errorString));
    setAppState(AppState::Error);
    setAppState(AppState::Ready);
}

void MainWindow::onRecorderError(QMediaRecorder::Error error, const QString &errorString)
{
    Q_UNUSED(error);
    QMessageBox::critical(this, tr("Recording Error"),
                          tr("Could not start recording:\n%1").arg(errorString));
    setAppState(AppState::Error);
    setAppState(AppState::Ready);
}

void MainWindow::onSeek(int value)
{
    m_player->setPosition(value);
}

void MainWindow::setAppState(AppState state)
{
    m_state = state;
    updateControls();

    switch (state) {
    case AppState::Ready:
        m_statusLabel->setText(tr("Status: Ready"));
        break;
    case AppState::Playing:
        m_statusLabel->setText(tr("Status: Playing"));
        break;
    case AppState::Paused:
        m_statusLabel->setText(tr("Status: Paused"));
        break;
    case AppState::Recording:
        m_statusLabel->setText(tr("Status: Recording"));
        break;
    case AppState::Error:
        m_statusLabel->setText(tr("Status: Error"));
        break;
    }
}

void MainWindow::updateControls()
{
    const bool ready = (m_state == AppState::Ready || m_state == AppState::Error);
    const bool playing = (m_state == AppState::Playing || m_state == AppState::Paused);
    const bool recording = (m_state == AppState::Recording);

    m_recordButton->setEnabled(ready || recording);
    m_playButton->setEnabled(ready || playing);
    m_stopButton->setEnabled(playing || recording);

    m_fileEdit->setEnabled(ready);
    m_browseButton->setEnabled(ready);
    m_inputCombo->setEnabled(ready || recording);
    m_outputCombo->setEnabled(ready || playing);

    m_seekSlider->setEnabled(playing);

    if (recording) {
        m_recordButton->setText(tr("● Recording…"));
        m_playButton->setEnabled(false);
    } else {
        m_recordButton->setText(tr("● Record"));
    }

    if (m_state == AppState::Paused) {
        m_playButton->setText(tr("▶ Resume"));
    } else {
        m_playButton->setText(tr("▶ Play"));
    }
}

void MainWindow::updateTimeLabel()
{
    if (m_state == AppState::Recording) {
        return;
    }
    const qint64 pos = m_player->position();
    m_timeLabel->setText(tr("%1 / %2").arg(formatTime(pos), formatTime(m_duration)));
}

QString MainWindow::formatTime(qint64 ms) const
{
    if (ms < 0) {
        ms = 0;
    }
    const int totalSecs = static_cast<int>(ms / 1000);
    const int mins = totalSecs / 60;
    const int secs = totalSecs % 60;
    return QStringLiteral("%1:%2")
        .arg(mins, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

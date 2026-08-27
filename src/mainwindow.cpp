// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "levelmeter.h"
#include "waveformwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QMediaFormat>
#include <QAudioDevice>
#include <QStyle>
#include <QIcon>
#include <QApplication>
#include <QAudioFormat>
#include <QAudioBuffer>
#include <QPainter>
#include <QPixmap>
#include <QKeySequence>
#include <QCloseEvent>
#include <QTemporaryFile>
#include <QtMath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("QWavRec"));
    setMinimumSize(520, 440);

    QIcon appIcon = QIcon::fromTheme(QStringLiteral("qwavrec"));
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral("/usr/share/icons/hicolor/scalable/apps/qwavrec.svg"));
    if (!appIcon.isNull())
        setWindowIcon(appIcon);

    createActions();
    createMenus();
    createToolBar();
    createCentralWidget();

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8);

    m_bufferOutput = new QAudioBufferOutput(this);
    m_player->setAudioBufferOutput(m_bufferOutput);
    connect(m_bufferOutput, &QAudioBufferOutput::audioBufferReceived,
            this, &MainWindow::onAudioBufferReceived);

    m_captureSession = new QMediaCaptureSession(this);
    m_audioInput = new QAudioInput(this);
    m_captureSession->setAudioInput(m_audioInput);
    m_audioInput->setVolume(1.0);
    m_recorder = new QMediaRecorder(this);
    m_captureSession->setRecorder(m_recorder);

    connect(&m_devices, &QMediaDevices::audioInputsChanged, this, &MainWindow::updateAudioDevices);
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, &MainWindow::updateAudioDevices);
    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputDeviceChanged);
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);
    connect(m_inputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onInputVolumeChanged);
    connect(m_outputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onOutputVolumeChanged);

    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::onPlayerStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MainWindow::onPlayerError);
    connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(m_recorder, &QMediaRecorder::durationChanged, this, [this](qint64 d) {
        if (m_state == AppState::Recording) {
            m_timeLabel->setText(formatTime(d));
            m_waveform->setDurationMs(d);
            m_duration = d;
        }
    });
    connect(m_recorder, &QMediaRecorder::errorOccurred, this, &MainWindow::onRecorderError);

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_seeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        onSeek(m_seekSlider->value());
    });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int v) {
        if (m_seeking) {
            m_timeLabel->setText(tr("%1 / %2").arg(formatTime(v), formatTime(m_duration)));
            if (m_duration > 0)
                m_waveform->setPlaybackPosition(static_cast<qreal>(v) / m_duration);
        }
    });

    updateAudioDevices();
    m_inputVolumeSlider->setValue(100);
    m_outputVolumeSlider->setValue(80);
    setAppState(AppState::Ready);
    startInputMonitoring();
    updateWindowTitle();
}

MainWindow::~MainWindow()
{
    stopInputMonitoring();
    if (m_state == AppState::Recording)
        m_recorder->stop();
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();
    if (!m_tempPath.isEmpty() && m_isTemporary)
        QFile::remove(m_tempPath);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::createActions()
{
    const QStyle *s = style();

    m_newAction = new QAction(s->standardIcon(QStyle::SP_FileIcon), tr("&New"), this);
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setStatusTip(tr("Discard current recording and start fresh"));
    connect(m_newAction, &QAction::triggered, this, &MainWindow::onNew);

    m_openAction = new QAction(s->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("Open an audio file"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpen);

    m_saveAction = new QAction(s->standardIcon(QStyle::SP_DialogSaveButton), tr("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip(tr("Save the current recording"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSave);

    m_saveAsAction = new QAction(tr("Save &As…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setStatusTip(tr("Save the current recording under a new name"));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);

    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_recordAction = new QAction(tr("Record"), this);
    m_recordAction->setStatusTip(tr("Start or stop recording"));
    {
        QPixmap pix(48, 48);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(200, 30, 30));
        painter.setPen(QPen(QColor(100, 15, 15), 2));
        painter.drawEllipse(4, 4, 40, 40);
        painter.setBrush(QColor(230, 50, 50));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(14, 14, 20, 20);
        m_recordAction->setIcon(QIcon(pix));
    }
    m_recordAction->setCheckable(true);
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    m_playAction = new QAction(s->standardIcon(QStyle::SP_MediaPlay), tr("Play"), this);
    m_playAction->setStatusTip(tr("Play or pause the current recording"));
    m_playAction->setCheckable(true);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);

    m_aboutAction = new QAction(tr("&About QWavRec"), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *transportMenu = menuBar()->addMenu(tr("&Transport"));
    transportMenu->addAction(m_recordAction);
    transportMenu->addAction(m_playAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setIconSize(QSize(24, 24));
    tb->addAction(m_newAction);
    tb->addAction(m_openAction);
    tb->addAction(m_saveAction);
    tb->addSeparator();
    tb->addAction(m_recordAction);
    tb->addAction(m_playAction);
}

void MainWindow::createCentralWidget()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);

    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("Document:")));
    m_fileLabel = new QLabel(tr("Untitled"));
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fileLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
    fileRow->addWidget(m_fileLabel, 1);
    mainLayout->addLayout(fileRow);

    auto *deviceForm = new QFormLayout;
    m_inputCombo = new QComboBox;
    m_outputCombo = new QComboBox;
    deviceForm->addRow(tr("Input"), m_inputCombo);
    deviceForm->addRow(tr("Output"), m_outputCombo);
    mainLayout->addLayout(deviceForm);

    // Volumes
    auto *volForm = new QFormLayout;
    m_inputVolumeSlider = new QSlider(Qt::Horizontal);
    m_inputVolumeSlider->setRange(0, 100);
    m_inputVolumeSlider->setToolTip(tr("Microphone / input level"));
    m_outputVolumeSlider = new QSlider(Qt::Horizontal);
    m_outputVolumeSlider->setRange(0, 100);
    m_outputVolumeSlider->setToolTip(tr("Playback volume"));
    volForm->addRow(tr("Mic level"), m_inputVolumeSlider);
    volForm->addRow(tr("Playback volume"), m_outputVolumeSlider);
    mainLayout->addLayout(volForm);

    auto *meterForm = new QFormLayout;
    m_inputMeter = new LevelMeter;
    m_outputMeter = new LevelMeter;
    meterForm->addRow(tr("Input"), m_inputMeter);
    meterForm->addRow(tr("Output"), m_outputMeter);
    mainLayout->addLayout(meterForm);

    m_waveform = new WaveformWidget;
    mainLayout->addWidget(m_waveform, 1);

    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    mainLayout->addWidget(m_seekSlider);

    m_timeLabel = new QLabel(tr("00:00 / 00:00"));
    m_timeLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_timeLabel);

    // Icon-only big transport buttons
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(16);
    auto makeBig = [](QAction *a) {
        auto *btn = new QToolButton;
        btn->setDefaultAction(a);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setIconSize(QSize(56, 56));
        btn->setMinimumSize(88, 88);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return btn;
    };
    buttonLayout->addStretch();
    buttonLayout->addWidget(makeBig(m_recordAction));
    buttonLayout->addWidget(makeBig(m_playAction));
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    statusBar()->showMessage(tr("Ready"));
}

// ---- Document helpers ----

bool MainWindow::hasDocument() const
{
    return !m_tempPath.isEmpty() || !m_savedPath.isEmpty();
}

QString MainWindow::documentPathForPlayback() const
{
    if (!m_tempPath.isEmpty() && QFileInfo::exists(m_tempPath))
        return m_tempPath;
    if (!m_savedPath.isEmpty() && QFileInfo::exists(m_savedPath))
        return m_savedPath;
    return {};
}

void MainWindow::setDocumentPath(const QString &path, bool isTemporary)
{
    if (!m_tempPath.isEmpty() && m_isTemporary && m_tempPath != path)
        QFile::remove(m_tempPath);

    if (isTemporary) {
        m_tempPath = path;
        m_isTemporary = true;
    } else {
        m_savedPath = path;
        m_tempPath.clear();
        m_isTemporary = false;
        m_modified = false;
    }
    m_fileLabel->setText(isTemporary ? tr("Untitled (unsaved)") : QFileInfo(path).fileName());
    m_fileLabel->setToolTip(path);
    updateWindowTitle();
}

void MainWindow::clearDocument()
{
    if (m_state == AppState::Recording)
        m_recorder->stop();
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();

    if (!m_tempPath.isEmpty() && m_isTemporary)
        QFile::remove(m_tempPath);

    m_tempPath.clear();
    m_savedPath.clear();
    m_isTemporary = true;
    m_modified = false;
    m_duration = 0;
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setValue(0);
    m_timeLabel->setText(tr("00:00 / 00:00"));
    m_waveform->clear();
    m_player->setSource(QUrl());
    m_fileLabel->setText(tr("Untitled"));
    m_fileLabel->setToolTip({});
    updateWindowTitle();
    setAppState(AppState::Ready);
}

void MainWindow::markModified()
{
    m_modified = true;
    updateWindowTitle();
}

bool MainWindow::maybeSave()
{
    if (!m_modified)
        return true;

    const auto ret = QMessageBox::warning(
        this, tr("QWavRec"),
        tr("The recording has not been saved.\nDo you want to save it?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (ret == QMessageBox::Save)
        return onSave(), !m_modified; // onSave clears modified on success
    if (ret == QMessageBox::Cancel)
        return false;
    return true; // Discard
}

// ---- File actions ----

void MainWindow::onNew()
{
    if (!maybeSave())
        return;
    clearDocument();
}

void MainWindow::onOpen()
{
    if (m_state == AppState::Recording)
        return;
    if (!maybeSave())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Audio File"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        tr("Audio Files (*.wav *.flac *.ogg *.mp3 *.m4a *.opus);;All Files (*)"));
    if (path.isEmpty())
        return;

    clearDocument();
    setDocumentPath(path, false);
    m_player->setSource(QUrl::fromLocalFile(path));
    m_waveform->clear();
    setAppState(AppState::Ready);
}

void MainWindow::onSave()
{
    if (!hasDocument() && m_tempPath.isEmpty())
        return;

    if (m_savedPath.isEmpty() || m_isTemporary) {
        onSaveAs();
        return;
    }

    // Already has a saved path; if we recorded into temp, copy over
    if (!m_tempPath.isEmpty() && m_tempPath != m_savedPath) {
        if (QFile::exists(m_savedPath))
            QFile::remove(m_savedPath);
        if (!QFile::copy(m_tempPath, m_savedPath)) {
            QMessageBox::critical(this, tr("Error"), tr("Could not save to:\n%1").arg(m_savedPath));
            return;
        }
        QFile::remove(m_tempPath);
        m_tempPath.clear();
        m_isTemporary = false;
    }
    m_modified = false;
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved"), 3000);
}

void MainWindow::onSaveAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Recording As"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/recording.wav"),
        tr("WAV Audio (*.wav);;All Files (*)"));
    if (path.isEmpty())
        return;

    QString source = m_tempPath;
    if (source.isEmpty())
        source = m_savedPath;
    if (source.isEmpty() || !QFileInfo::exists(source)) {
        QMessageBox::warning(this, tr("Error"), tr("Nothing to save yet."));
        return;
    }

    if (QFile::exists(path) && path != source)
        QFile::remove(path);
    if (!QFile::copy(source, path)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not save to:\n%1").arg(path));
        return;
    }

    if (!m_tempPath.isEmpty() && m_isTemporary && m_tempPath != path)
        QFile::remove(m_tempPath);

    m_tempPath.clear();
    m_savedPath = path;
    m_isTemporary = false;
    m_modified = false;
    m_fileLabel->setText(QFileInfo(path).fileName());
    m_fileLabel->setToolTip(path);
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved"), 3000);
}

// ---- Transport (toggles) ----

void MainWindow::onRecord()
{
    if (m_state == AppState::Recording) {
        // Toggle off → stop
        m_recorder->stop();
        return;
    }

    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();

    // Ensure we have a temp file to record into
    if (m_tempPath.isEmpty() || !m_isTemporary) {
        QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("qwavrec-XXXXXX.wav")));
        tmp.setAutoRemove(false);
        if (!tmp.open()) {
            QMessageBox::critical(this, tr("Error"), tr("Could not create temporary file."));
            m_recordAction->setChecked(false);
            return;
        }
        m_tempPath = tmp.fileName();
        tmp.close();
        m_isTemporary = true;
        m_fileLabel->setText(tr("Untitled (unsaved)"));
        m_fileLabel->setToolTip(m_tempPath);
    }

    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_tempPath));
    QMediaFormat format;
    format.setFileFormat(QMediaFormat::Wave);
    format.setAudioCodec(QMediaFormat::AudioCodec::Wave);
    m_recorder->setMediaFormat(format);
    m_recorder->setQuality(QMediaRecorder::HighQuality);

    m_waveform->clear();
    stopInputMonitoring();
    m_recorder->record();
    // state change will set Recording + checked
}

void MainWindow::onPlay()
{
    if (m_state == AppState::Recording) {
        m_playAction->setChecked(false);
        return;
    }

    if (m_state == AppState::Playing) {
        m_player->pause();
        return;
    }
    if (m_state == AppState::Paused) {
        m_player->play();
        return;
    }

    const QString path = documentPathForPlayback();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(this, tr("QWavRec"),
            tr("Nothing to play. Record something or open a file first."));
        m_playAction->setChecked(false);
        return;
    }

    if (m_player->source().toLocalFile() != path)
        m_player->setSource(QUrl::fromLocalFile(path));
    m_waveform->clear();
    m_player->play();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About QWavRec"),
        tr("<h3>QWavRec</h3>"
           "<p>Simple PipeWire audio player and recorder.</p>"
           "<p>Recordings stay temporary until you save them.</p>"
           "<p>Version %1</p>"
           "<p>License: GPL-3.0-or-later</p>")
            .arg(QApplication::applicationVersion()));
}

// ---- Device / volume ----

void MainWindow::updateAudioDevices()
{
    QByteArray curIn, curOut;
    if (m_inputCombo->currentIndex() >= 0)
        curIn = m_inputCombo->currentData().toByteArray();
    if (m_outputCombo->currentIndex() >= 0)
        curOut = m_outputCombo->currentData().toByteArray();

    m_inputCombo->blockSignals(true);
    m_outputCombo->blockSignals(true);
    m_inputCombo->clear();
    int selIn = 0, i = 0;
    for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
        m_inputCombo->addItem(dev.description(), dev.id());
        if (dev.id() == curIn || (curIn.isEmpty() && dev.isDefault()))
            selIn = i;
        ++i;
    }
    if (m_inputCombo->count()) {
        m_inputCombo->setCurrentIndex(selIn);
        onInputDeviceChanged(selIn);
    }

    m_outputCombo->clear();
    int selOut = 0;
    i = 0;
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        m_outputCombo->addItem(dev.description(), dev.id());
        if (dev.id() == curOut || (curOut.isEmpty() && dev.isDefault()))
            selOut = i;
        ++i;
    }
    if (m_outputCombo->count()) {
        m_outputCombo->setCurrentIndex(selOut);
        onOutputDeviceChanged(selOut);
    }
    m_inputCombo->blockSignals(false);
    m_outputCombo->blockSignals(false);
}

void MainWindow::onInputDeviceChanged(int index)
{
    if (index < 0) return;
    const QByteArray id = m_inputCombo->itemData(index).toByteArray();
    for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
        if (dev.id() == id) {
            m_audioInput->setDevice(dev);
            if (m_state != AppState::Recording) {
                stopInputMonitoring();
                startInputMonitoring();
            } else {
                m_recorder->stop();
            }
            break;
        }
    }
}

void MainWindow::onOutputDeviceChanged(int index)
{
    if (index < 0) return;
    const QByteArray id = m_outputCombo->itemData(index).toByteArray();
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        if (dev.id() == id) {
            m_audioOutput->setDevice(dev);
            break;
        }
    }
}

void MainWindow::onInputVolumeChanged(int value)
{
    m_audioInput->setVolume(value / 100.0);
}

void MainWindow::onOutputVolumeChanged(int value)
{
    m_audioOutput->setVolume(value / 100.0);
}

// ---- Player / recorder state ----

void MainWindow::onPositionChanged(qint64 position)
{
    if (!m_seeking && (m_state == AppState::Playing || m_state == AppState::Paused)) {
        m_seekSlider->setValue(static_cast<int>(position));
        updateTimeLabel();
        if (m_duration > 0)
            m_waveform->setPlaybackPosition(static_cast<qreal>(position) / m_duration);
    }
}

void MainWindow::onDurationChanged(qint64 duration)
{
    m_duration = duration;
    m_seekSlider->setRange(0, static_cast<int>(duration));
    m_waveform->setDurationMs(duration);
    updateTimeLabel();
}

void MainWindow::onPlayerStateChanged(QMediaPlayer::PlaybackState state)
{
    switch (state) {
    case QMediaPlayer::PlayingState:
        setAppState(AppState::Playing);
        m_playAction->setChecked(true);
        m_playAction->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        break;
    case QMediaPlayer::PausedState:
        setAppState(AppState::Paused);
        m_playAction->setChecked(true);
        m_playAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        break;
    case QMediaPlayer::StoppedState:
        if (m_state != AppState::Recording) {
            setAppState(AppState::Ready);
            m_playAction->setChecked(false);
            m_playAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            m_seekSlider->setValue(0);
            m_waveform->setPlaybackPosition(0.0);
            updateTimeLabel();
            m_outputMeter->setLevel(0.0);
        }
        break;
    }
}

void MainWindow::onRecorderStateChanged(QMediaRecorder::RecorderState state)
{
    switch (state) {
    case QMediaRecorder::RecordingState:
        setAppState(AppState::Recording);
        m_recordAction->setChecked(true);
        markModified();
        break;
    case QMediaRecorder::StoppedState:
        if (m_state == AppState::Recording) {
            setAppState(AppState::Ready);
            m_recordAction->setChecked(false);
            m_inputMeter->setLevel(0.0);
            // Point player at the new temp recording so Play works immediately
            if (!m_tempPath.isEmpty()) {
                m_player->setSource(QUrl::fromLocalFile(m_tempPath));
                m_fileLabel->setText(tr("Untitled (unsaved)"));
            }
            startInputMonitoring();
        }
        break;
    default:
        break;
    }
}

void MainWindow::onPlayerError(QMediaPlayer::Error, const QString &errorString)
{
    QMessageBox::critical(this, tr("Playback Error"),
                          tr("Could not play:\n%1").arg(errorString));
    m_playAction->setChecked(false);
    setAppState(AppState::Ready);
}

void MainWindow::onRecorderError(QMediaRecorder::Error, const QString &errorString)
{
    QMessageBox::critical(this, tr("Recording Error"),
                          tr("Could not record:\n%1").arg(errorString));
    m_recordAction->setChecked(false);
    setAppState(AppState::Ready);
    startInputMonitoring();
}

void MainWindow::onSeek(int value)
{
    m_player->setPosition(value);
}

// ---- Monitoring ----

void MainWindow::startInputMonitoring()
{
    stopInputMonitoring();
    QAudioDevice device;
    if (m_inputCombo->currentIndex() >= 0) {
        const QByteArray id = m_inputCombo->currentData().toByteArray();
        for (const QAudioDevice &d : QMediaDevices::audioInputs())
            if (d.id() == id) { device = d; break; }
    }
    if (device.isNull())
        device = QMediaDevices::defaultAudioInput();
    if (device.isNull())
        return;

    QAudioFormat format = device.preferredFormat();
    if (format.sampleFormat() == QAudioFormat::Unknown) {
        format.setSampleFormat(QAudioFormat::Int16);
        format.setSampleRate(48000);
        format.setChannelCount(1);
    }
    m_audioSource = new QAudioSource(device, format, this);
    m_audioSourceDevice = m_audioSource->start();
    if (m_audioSourceDevice)
        connect(m_audioSourceDevice, &QIODevice::readyRead, this, &MainWindow::onAudioSourceReadyRead);
}

void MainWindow::stopInputMonitoring()
{
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        m_audioSourceDevice = nullptr;
    }
}

void MainWindow::onAudioSourceReadyRead()
{
    if (!m_audioSourceDevice || !m_audioSource) return;
    const QByteArray data = m_audioSourceDevice->readAll();
    if (data.isEmpty()) return;
    const QAudioFormat fmt = m_audioSource->format();
    qreal sumSq = 0.0, peak = 0.0;
    if (fmt.sampleFormat() == QAudioFormat::Int16) {
        const auto *s = reinterpret_cast<const qint16 *>(data.constData());
        const int n = data.size() / int(sizeof(qint16));
        for (int i = 0; i < n; ++i) {
            const qreal v = s[i] / 32768.0;
            sumSq += v * v;
            peak = qMax(peak, qAbs(v));
        }
        if (n > 0) {
            m_inputMeter->setLevel(qSqrt(sumSq / n) * 2.5);
            m_waveform->addLevel(peak);
        }
    } else if (fmt.sampleFormat() == QAudioFormat::Float) {
        const auto *s = reinterpret_cast<const float *>(data.constData());
        const int n = data.size() / int(sizeof(float));
        for (int i = 0; i < n; ++i) {
            sumSq += double(s[i]) * s[i];
            peak = qMax(peak, qAbs(static_cast<qreal>(s[i])));
        }
        if (n > 0) {
            m_inputMeter->setLevel(qSqrt(sumSq / n) * 2.5);
            m_waveform->addLevel(peak);
        }
    }
}

void MainWindow::onAudioBufferReceived(const QAudioBuffer &buffer)
{
    if (m_state != AppState::Playing && m_state != AppState::Paused)
        return;
    const qreal level = computeLevel(buffer);
    m_outputMeter->setLevel(level);
    m_waveform->addLevel(level);
}

qreal MainWindow::computeLevel(const QAudioBuffer &buffer) const
{
    if (!buffer.isValid() || buffer.sampleCount() == 0)
        return 0.0;
    const QAudioFormat fmt = buffer.format();
    qreal sumSq = 0.0;
    const int n = buffer.sampleCount();
    if (fmt.sampleFormat() == QAudioFormat::Int16) {
        const auto *s = buffer.constData<qint16>();
        for (int i = 0; i < n; ++i) {
            const qreal v = s[i] / 32768.0;
            sumSq += v * v;
        }
    } else if (fmt.sampleFormat() == QAudioFormat::Float) {
        const auto *s = buffer.constData<float>();
        for (int i = 0; i < n; ++i)
            sumSq += double(s[i]) * s[i];
    } else
        return 0.0;
    return qMin(1.0, qSqrt(sumSq / n) * 2.5);
}

void MainWindow::setAppState(AppState state)
{
    m_state = state;
    updateControls();
    switch (state) {
    case AppState::Ready:
        statusBar()->showMessage(tr("Ready"));
        break;
    case AppState::Playing:
        statusBar()->showMessage(tr("Playing"));
        break;
    case AppState::Paused:
        statusBar()->showMessage(tr("Paused"));
        break;
    case AppState::Recording:
        statusBar()->showMessage(tr("Recording…"));
        break;
    case AppState::Error:
        statusBar()->showMessage(tr("Error"));
        break;
    }
}

void MainWindow::updateControls()
{
    const bool ready = (m_state == AppState::Ready || m_state == AppState::Error);
    const bool playing = (m_state == AppState::Playing || m_state == AppState::Paused);
    const bool recording = (m_state == AppState::Recording);

    m_newAction->setEnabled(ready);
    m_openAction->setEnabled(ready);
    m_saveAction->setEnabled(ready && (m_modified || hasDocument()));
    m_saveAsAction->setEnabled(ready && hasDocument());
    m_recordAction->setEnabled(ready || recording);
    m_playAction->setEnabled((ready || playing) && (hasDocument() || !documentPathForPlayback().isEmpty()));
    m_inputCombo->setEnabled(ready || recording);
    m_outputCombo->setEnabled(ready || playing);
    m_seekSlider->setEnabled(playing);
    m_inputVolumeSlider->setEnabled(true);
    m_outputVolumeSlider->setEnabled(true);
}

void MainWindow::updateTimeLabel()
{
    if (m_state == AppState::Recording)
        return;
    m_timeLabel->setText(tr("%1 / %2")
        .arg(formatTime(m_player->position()), formatTime(m_duration)));
}

void MainWindow::updateWindowTitle()
{
    QString name = m_savedPath.isEmpty() ? tr("Untitled") : QFileInfo(m_savedPath).fileName();
    if (m_modified || m_isTemporary)
        name += QChar(u'*');
    setWindowTitle(tr("%1 — QWavRec").arg(name));
}

QString MainWindow::formatTime(qint64 ms) const
{
    if (ms < 0) ms = 0;
    const int totalSecs = static_cast<int>(ms / 1000);
    return QStringLiteral("%1:%2")
        .arg(totalSecs / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSecs % 60, 2, 10, QLatin1Char('0'));
}

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
#include <QtMath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("QWavRec"));
    setMinimumSize(520, 420);

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

    m_bufferOutput = new QAudioBufferOutput(this);
    m_player->setAudioBufferOutput(m_bufferOutput);
    connect(m_bufferOutput, &QAudioBufferOutput::audioBufferReceived,
            this, &MainWindow::onAudioBufferReceived);

    m_captureSession = new QMediaCaptureSession(this);
    m_audioInput = new QAudioInput(this);
    m_captureSession->setAudioInput(m_audioInput);
    m_recorder = new QMediaRecorder(this);
    m_captureSession->setRecorder(m_recorder);

    connect(&m_devices, &QMediaDevices::audioInputsChanged, this, &MainWindow::updateAudioDevices);
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, &MainWindow::updateAudioDevices);
    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputDeviceChanged);
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);

    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::onPlayerStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MainWindow::onPlayerError);
    connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(m_recorder, &QMediaRecorder::durationChanged, this, [this](qint64 d) {
        if (m_state == AppState::Recording) {
            m_timeLabel->setText(formatTime(d));
            m_waveform->setDurationMs(d);
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
    setAppState(AppState::Ready);
    startInputMonitoring();
    updateWindowTitle();
}

MainWindow::~MainWindow()
{
    stopInputMonitoring();
    if (m_state == AppState::Recording) m_recorder->stop();
    if (m_state == AppState::Playing || m_state == AppState::Paused) m_player->stop();
}

void MainWindow::createActions()
{
    const QStyle *s = style();
    m_openAction = new QAction(s->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpen);

    m_saveAsAction = new QAction(s->standardIcon(QStyle::SP_DialogSaveButton), tr("&Save Recording As…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);

    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_recordAction = new QAction(tr("Record"), this);
    {
        QPixmap pix(32, 32);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(200, 30, 30));
        painter.setPen(QPen(QColor(120, 20, 20), 2));
        painter.drawEllipse(4, 4, 24, 24);
        painter.setBrush(QColor(230, 50, 50));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(10, 10, 12, 12);
        m_recordAction->setIcon(QIcon(pix));
    }
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    m_playAction = new QAction(s->standardIcon(QStyle::SP_MediaPlay), tr("Play"), this);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);

    m_stopAction = new QAction(s->standardIcon(QStyle::SP_MediaStop), tr("Stop"), this);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);

    m_aboutAction = new QAction(tr("&About QWavRec"), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *transportMenu = menuBar()->addMenu(tr("&Transport"));
    transportMenu->addAction(m_recordAction);
    transportMenu->addAction(m_playAction);
    transportMenu->addAction(m_stopAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setIconSize(QSize(24, 24));
    tb->addAction(m_openAction);
    tb->addAction(m_saveAsAction);
    tb->addSeparator();
    tb->addAction(m_recordAction);
    tb->addAction(m_playAction);
    tb->addAction(m_stopAction);
}

void MainWindow::createCentralWidget()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);

    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("File:")));
    m_fileLabel = new QLabel(tr("(none)"));
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

    auto *meterForm = new QFormLayout;
    m_inputMeter = new LevelMeter;
    m_outputMeter = new LevelMeter;
    meterForm->addRow(tr("Input level"), m_inputMeter);
    meterForm->addRow(tr("Output level"), m_outputMeter);
    mainLayout->addLayout(meterForm);

    m_waveform = new WaveformWidget;
    mainLayout->addWidget(m_waveform, 1);

    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    mainLayout->addWidget(m_seekSlider);

    m_timeLabel = new QLabel(tr("00:00 / 00:00"));
    m_timeLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_timeLabel);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(12);
    auto makeBig = [](QAction *a) {
        auto *btn = new QToolButton;
        btn->setDefaultAction(a);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIconSize(QSize(48, 48));
        btn->setMinimumSize(96, 80);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return btn;
    };
    buttonLayout->addWidget(makeBig(m_recordAction));
    buttonLayout->addWidget(makeBig(m_playAction));
    buttonLayout->addWidget(makeBig(m_stopAction));
    mainLayout->addLayout(buttonLayout);

    m_statusLabel = new QLabel(tr("Status: Ready"));
    mainLayout->addWidget(m_statusLabel);
    statusBar()->showMessage(tr("Ready"));
}

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
            stopInputMonitoring();
            startInputMonitoring();
            if (m_state == AppState::Recording)
                m_recorder->stop();
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

void MainWindow::onOpen()
{
    if (m_state == AppState::Recording) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Audio File"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        tr("Audio Files (*.wav *.flac *.ogg *.mp3 *.m4a *.opus);;All Files (*)"));
    if (path.isEmpty()) return;
    m_currentFile = path;
    m_fileLabel->setText(QFileInfo(path).fileName());
    m_fileLabel->setToolTip(path);
    updateWindowTitle();
    m_waveform->clear();
    m_player->setSource(QUrl::fromLocalFile(path));
    setAppState(AppState::Ready);
}

void MainWindow::onSaveAs()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Recording As"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/recording.wav"),
        tr("WAV Audio (*.wav);;All Files (*)"));
    if (path.isEmpty()) return;
    m_currentFile = path;
    m_fileLabel->setText(QFileInfo(path).fileName());
    m_fileLabel->setToolTip(path);
    updateWindowTitle();
}

void MainWindow::onRecord()
{
    if (m_state == AppState::Recording) return;
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();
    if (m_currentFile.isEmpty()) {
        onSaveAs();
        if (m_currentFile.isEmpty()) return;
    }
    QFileInfo fi(m_currentFile);
    QDir().mkpath(fi.absolutePath());
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_currentFile));
    QMediaFormat format;
    format.setFileFormat(QMediaFormat::Wave);
    format.setAudioCodec(QMediaFormat::AudioCodec::Wave);
    m_recorder->setMediaFormat(format);
    m_recorder->setQuality(QMediaRecorder::HighQuality);
    m_waveform->clear();
    m_recorder->record();
}

void MainWindow::onPlay()
{
    if (m_state == AppState::Recording) return;
    if (m_currentFile.isEmpty() || !QFileInfo::exists(m_currentFile)) {
        QMessageBox::warning(this, tr("Error"), tr("Please open an existing audio file first."));
        return;
    }
    if (m_state == AppState::Paused) {
        m_player->play();
        return;
    }
    if (m_player->source().toLocalFile() != m_currentFile)
        m_player->setSource(QUrl::fromLocalFile(m_currentFile));
    m_waveform->clear();
    m_player->play();
}

void MainWindow::onStop()
{
    if (m_state == AppState::Recording)
        m_recorder->stop();
    else if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About QWavRec"),
        tr("<h3>QWavRec</h3><p>Simple PipeWire audio player and recorder.</p>"
           "<p>Version %1</p><p>License: GPL-3.0-or-later</p>")
            .arg(QApplication::applicationVersion()));
}

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
    case QMediaPlayer::PlayingState: setAppState(AppState::Playing); break;
    case QMediaPlayer::PausedState: setAppState(AppState::Paused); break;
    case QMediaPlayer::StoppedState:
        if (m_state != AppState::Recording) {
            setAppState(AppState::Ready);
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
    case QMediaRecorder::RecordingState: setAppState(AppState::Recording); break;
    case QMediaRecorder::StoppedState:
        if (m_state == AppState::Recording) {
            setAppState(AppState::Ready);
            m_inputMeter->setLevel(0.0);
        }
        break;
    default: break;
    }
}

void MainWindow::onPlayerError(QMediaPlayer::Error, const QString &errorString)
{
    QMessageBox::critical(this, tr("Playback Error"), tr("Could not play file:\n%1").arg(errorString));
    setAppState(AppState::Ready);
}

void MainWindow::onRecorderError(QMediaRecorder::Error, const QString &errorString)
{
    QMessageBox::critical(this, tr("Recording Error"), tr("Could not start recording:\n%1").arg(errorString));
    setAppState(AppState::Ready);
}

void MainWindow::onSeek(int value) { m_player->setPosition(value); }

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
    if (device.isNull()) return;

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
            sumSq += s[i] * s[i];
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
    if (m_state != AppState::Playing && m_state != AppState::Paused) return;
    const qreal level = computeLevel(buffer);
    m_outputMeter->setLevel(level);
    m_waveform->addLevel(level);
}

qreal MainWindow::computeLevel(const QAudioBuffer &buffer) const
{
    if (!buffer.isValid() || buffer.sampleCount() == 0) return 0.0;
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
            sumSq += s[i] * s[i];
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
        m_statusLabel->setText(tr("Status: Ready"));
        statusBar()->showMessage(tr("Ready"));
        break;
    case AppState::Playing:
        m_statusLabel->setText(tr("Status: Playing"));
        statusBar()->showMessage(tr("Playing"));
        break;
    case AppState::Paused:
        m_statusLabel->setText(tr("Status: Paused"));
        statusBar()->showMessage(tr("Paused"));
        break;
    case AppState::Recording:
        m_statusLabel->setText(tr("Status: Recording"));
        statusBar()->showMessage(tr("Recording…"));
        break;
    case AppState::Error:
        m_statusLabel->setText(tr("Status: Error"));
        statusBar()->showMessage(tr("Error"));
        break;
    }
}

void MainWindow::updateControls()
{
    const bool ready = (m_state == AppState::Ready || m_state == AppState::Error);
    const bool playing = (m_state == AppState::Playing || m_state == AppState::Paused);
    const bool recording = (m_state == AppState::Recording);

    m_openAction->setEnabled(ready);
    m_saveAsAction->setEnabled(ready);
    m_recordAction->setEnabled(ready || recording);
    m_playAction->setEnabled(ready || playing);
    m_stopAction->setEnabled(playing || recording);
    m_inputCombo->setEnabled(ready || recording);
    m_outputCombo->setEnabled(ready || playing);
    m_seekSlider->setEnabled(playing);

    if (recording) {
        m_recordAction->setText(tr("Recording…"));
        m_playAction->setEnabled(false);
    } else
        m_recordAction->setText(tr("Record"));

    if (m_state == AppState::Paused)
        m_playAction->setText(tr("Resume"));
    else
        m_playAction->setText(tr("Play"));
}

void MainWindow::updateTimeLabel()
{
    if (m_state == AppState::Recording) return;
    m_timeLabel->setText(tr("%1 / %2").arg(formatTime(m_player->position()), formatTime(m_duration)));
}

void MainWindow::updateWindowTitle()
{
    if (m_currentFile.isEmpty())
        setWindowTitle(tr("QWavRec"));
    else
        setWindowTitle(tr("%1 — QWavRec").arg(QFileInfo(m_currentFile).fileName()));
}

QString MainWindow::formatTime(qint64 ms) const
{
    if (ms < 0) ms = 0;
    const int totalSecs = static_cast<int>(ms / 1000);
    return QStringLiteral("%1:%2")
        .arg(totalSecs / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSecs % 60, 2, 10, QLatin1Char('0'));
}

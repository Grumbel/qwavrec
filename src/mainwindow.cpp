// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include "levelmeter.h"
#include "waveformwidget.h"
#include "markedslider.h"
#include "recordinghistory.h"
#include "wavfile.h"
#include "historydialog.h"
#include "pulsebackend.h"

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
#include <QAudioFormat>
#include <QStyle>
#include <QIcon>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QKeySequence>
#include <QCloseEvent>
#include <QTemporaryFile>
#include <QSettings>
#include <QtMath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("QWavRec"));
    setMinimumSize(520, 460);

    QIcon appIcon = QIcon::fromTheme(QStringLiteral("qwavrec"));
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral("/usr/share/icons/hicolor/scalable/apps/qwavrec.svg"));
    if (!appIcon.isNull())
        setWindowIcon(appIcon);

    createActions();
    createMenus();
    createToolBar();
    createCentralWidget();

    m_player = new PulsePlayback(this);
    connect(m_player, &PulsePlayback::stateChanged, this, &MainWindow::onPlayerStateChanged);
    connect(m_player, &PulsePlayback::positionChanged, this, &MainWindow::onPlayerPosition);
    connect(m_player, &PulsePlayback::durationChanged, this, &MainWindow::onPlayerDuration);
    connect(m_player, &PulsePlayback::errorOccurred, this, &MainWindow::onPlayerError);

    m_capture = new PulseCapture(this);
    connect(m_capture, &PulseCapture::errorOccurred, this, &MainWindow::onCaptureError);

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(50); // 20 Hz — smooth meter, no event flood
    connect(m_meterTimer, &QTimer::timeout, this, &MainWindow::onMeterTick);
    m_meterTimer->start();

    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputDeviceChanged);
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);
    connect(m_waveform, &WaveformWidget::seekRequested, this, &MainWindow::onWaveformSeek);
    connect(m_waveform, &WaveformWidget::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_inputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onInputVolumeChanged);
    connect(m_outputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onOutputVolumeChanged);

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_seeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        onSeek(m_seekSlider->value());
    });
    // Clicking the groove updates value then fires this — jump immediately
    connect(m_seekSlider, &QSlider::actionTriggered, this, [this](int) {
        if (m_seeking)
            return;
        onSeek(m_seekSlider->value());
        if (m_duration > 0)
            m_waveform->setPlaybackPosition(double(m_seekSlider->value()) / m_duration);
    });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int v) {
        if (m_seeking) {
            m_timeLabel->setText(tr("%1 / %2").arg(formatTime(v), formatTime(m_duration)));
            if (m_duration > 0)
                m_waveform->setPlaybackPosition(static_cast<qreal>(v) / m_duration);
        }
    });

    loadSettings();
    refreshDevices();
    setAppState(AppState::Ready);
    startMonitoring();
    updateWindowTitle();
}

MainWindow::~MainWindow()
{
    saveSettings();
    if (m_state == AppState::Recording)
        onRecord();
    stopMonitoring();
    if (m_player)
        m_player->stop();
    if (m_capture)
        m_capture->stop();
    if (!m_tempPath.isEmpty() && m_isTemporary)
        QFile::remove(m_tempPath);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    // Takes are already archived in the cache — no discard prompt.
    event->accept();
}

QIcon MainWindow::themeIcon(const QString &name, QStyle::StandardPixmap fallback) const
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        icon = style()->standardIcon(fallback);
    return icon;
}

void MainWindow::createActions()
{
    m_newAction = new QAction(themeIcon(QStringLiteral("document-new"), QStyle::SP_FileIcon),
                              tr("&New"), this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::onNew);

    m_openAction = new QAction(themeIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton),
                               tr("&Open…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpen);

    m_saveAction = new QAction(themeIcon(QStringLiteral("document-save"), QStyle::SP_DialogSaveButton),
                               tr("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSave);

    m_saveAsAction = new QAction(themeIcon(QStringLiteral("document-save-as"), QStyle::SP_DialogSaveButton),
                                 tr("Save &As…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);

    m_undoAction = new QAction(themeIcon(QStringLiteral("go-previous"), QStyle::SP_ArrowBack),
                               tr("&Previous Take"), this);
    m_undoAction->setStatusTip(tr("Load the previous take from the local cache history"));
    m_undoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::onUndo);

    m_redoAction = new QAction(themeIcon(QStringLiteral("go-next"), QStyle::SP_ArrowForward),
                               tr("&Next Take"), this);
    m_redoAction->setStatusTip(tr("Load the next take from the local cache history"));
    m_redoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::onRedo);

    m_historyAction = new QAction(themeIcon(QStringLiteral("view-list-details"), QStyle::SP_FileDialogDetailedView),
                                  tr("Take &History…"), this);
    m_historyAction->setStatusTip(tr("Browse all cached takes"));
    m_historyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(m_historyAction, &QAction::triggered, this, &MainWindow::onHistory);

    m_quitAction = new QAction(themeIcon(QStringLiteral("application-exit"), QStyle::SP_DialogCloseButton),
                               tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    // Record: red circle (no good freedesktop standard)
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
        m_recordAction = new QAction(QIcon(pix), tr("Record"), this);
    }
    m_recordAction->setStatusTip(tr("Start or stop recording"));
    m_recordAction->setCheckable(true);
    m_recordAction->setShortcut(QKeySequence(Qt::Key_R));
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    m_playAction = new QAction(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay),
                               tr("Play"), this);
    m_playAction->setStatusTip(tr("Play or pause"));
    m_playAction->setCheckable(true);
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);

    m_stopAction = new QAction(themeIcon(QStringLiteral("media-playback-stop"), QStyle::SP_MediaStop),
                               tr("Stop"), this);
    m_stopAction->setStatusTip(tr("Stop playback and return to the start"));
    m_stopAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);

    m_normalizeAction = new QAction(themeIcon(QStringLiteral("audio-volume-high"), QStyle::SP_MediaVolume),
                                    tr("&Normalize"), this);
    m_normalizeAction->setStatusTip(tr("Peak-normalize the current recording without clipping"));
    connect(m_normalizeAction, &QAction::triggered, this, &MainWindow::onNormalize);

    m_loopAction = new QAction(themeIcon(QStringLiteral("media-playlist-repeat"), QStyle::SP_BrowserReload),
                               tr("Loop"), this);
    m_loopAction->setStatusTip(tr("Repeat playback"));
    m_loopAction->setCheckable(true);
    connect(m_loopAction, &QAction::toggled, this, &MainWindow::onLoopToggled);

    m_autoScaleAction = new QAction(tr("Auto-&Scale Waveform"), this);
    m_autoScaleAction->setStatusTip(tr("Normalize waveform display so quiet recordings fill the view"));
    m_autoScaleAction->setCheckable(true);
    connect(m_autoScaleAction, &QAction::toggled, this, &MainWindow::onAutoScaleWaveformToggled);

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

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_normalizeAction);

    QMenu *transportMenu = menuBar()->addMenu(tr("&Transport"));
    transportMenu->addAction(m_recordAction);
    transportMenu->addAction(m_playAction);
    transportMenu->addAction(m_stopAction);
    transportMenu->addSeparator();
    transportMenu->addAction(m_undoAction);
    transportMenu->addAction(m_redoAction);
    transportMenu->addAction(m_historyAction);
    transportMenu->addSeparator();
    transportMenu->addAction(m_loopAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_historyAction);
    viewMenu->addAction(m_autoScaleAction);

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
    tb->addAction(m_undoAction);
    tb->addAction(m_redoAction);
    tb->addAction(m_historyAction);
    tb->addSeparator();
    tb->addAction(m_normalizeAction);
    tb->addAction(m_loopAction);
}

void MainWindow::createCentralWidget()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);

    auto *deviceForm = new QFormLayout;
    m_inputCombo = new QComboBox;
    m_outputCombo = new QComboBox;
    deviceForm->addRow(tr("Input"), m_inputCombo);
    deviceForm->addRow(tr("Output"), m_outputCombo);
    mainLayout->addLayout(deviceForm);

    auto *volForm = new QFormLayout;
    m_inputVolumeSlider = new MarkedSlider(Qt::Horizontal);
    m_inputVolumeSlider->setRange(0, 300); // 0..3×
    m_inputVolumeSlider->setMarkerValue(100); // unity
    m_inputVolumeSlider->setToolTip(tr("Microphone gain — yellow mark is unity (100%). Above = boost."));
    m_micGainLabel = new QLabel(tr("100%"));
    m_micGainLabel->setMinimumWidth(48);
    auto *micRow = new QHBoxLayout;
    micRow->addWidget(m_inputVolumeSlider, 1);
    micRow->addWidget(m_micGainLabel);
    m_outputVolumeSlider = new QSlider(Qt::Horizontal);
    m_outputVolumeSlider->setRange(0, 100);
    m_outputVolumeSlider->setToolTip(tr("Playback volume"));
    volForm->addRow(tr("Mic gain"), micRow);
    volForm->addRow(tr("Playback volume"), m_outputVolumeSlider);
    mainLayout->addLayout(volForm);

    auto *meterForm = new QFormLayout;
    m_inputMeter = new LevelMeter;
    m_outputMeter = new LevelMeter;
    meterForm->addRow(tr("Input"), m_inputMeter);
    meterForm->addRow(tr("Output"), m_outputMeter);
    mainLayout->addLayout(meterForm);

    m_waveform = new WaveformWidget;
    m_waveform->setToolTip(tr("Drag to select region A–B (playback/loop limited to selection).\n"
                              "Click to seek. Double-click to clear selection."));
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
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setIconSize(QSize(48, 48));
        btn->setMinimumSize(80, 80);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return btn;
    };
    buttonLayout->addStretch();
    buttonLayout->addWidget(makeBig(m_recordAction));
    buttonLayout->addWidget(makeBig(m_playAction));
    buttonLayout->addWidget(makeBig(m_stopAction));
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_pendingInputName = s.value(QStringLiteral("audio/inputName")).toString();
    m_pendingOutputName = s.value(QStringLiteral("audio/outputName")).toString();
    const int mic = s.value(QStringLiteral("audio/micGain"), 100).toInt();
    const int out = s.value(QStringLiteral("audio/playbackVolume"), 80).toInt();
    const bool loop = s.value(QStringLiteral("playback/loop"), false).toBool();
    const bool autoScale = s.value(QStringLiteral("view/autoScaleWaveform"), false).toBool();

    m_restoringSettings = true;
    m_inputVolumeSlider->setValue(qBound(0, mic, 300));
    m_outputVolumeSlider->setValue(qBound(0, out, 100));
    m_loopAction->setChecked(loop);
    m_player->setLoop(loop);
    m_autoScaleWaveform = autoScale;
    m_autoScaleAction->setChecked(autoScale);
    m_restoringSettings = false;
    updateMicGainLabel();
}

void MainWindow::saveSettings()
{
    QSettings s;
    if (m_inputCombo->currentIndex() >= 0)
        s.setValue(QStringLiteral("audio/inputName"), m_inputCombo->currentData().toString());
    if (m_outputCombo->currentIndex() >= 0)
        s.setValue(QStringLiteral("audio/outputName"), m_outputCombo->currentData().toString());
    s.setValue(QStringLiteral("audio/micGain"), m_inputVolumeSlider->value());
    s.setValue(QStringLiteral("audio/playbackVolume"), m_outputVolumeSlider->value());
    s.setValue(QStringLiteral("playback/loop"), m_loopAction->isChecked());
    s.setValue(QStringLiteral("view/autoScaleWaveform"), m_autoScaleWaveform);
}

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

void MainWindow::clearDocument()
{
    if (m_state == AppState::Recording)
        onRecord();
    if (m_player)
        m_player->stop();
    if (m_capture)
        m_capture->stop();

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
    m_liveRecordPeaks.clear();
    m_rawPeaks.clear();
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
    // Cache holds every take; no blocking prompt on New/Open/Quit.
    return true;
}

QVector<float> MainWindow::normalizedPeaks(const QVector<float> &raw) const
{
    QVector<float> peaks = raw;
    float mx = 0.f;
    for (float v : peaks)
        mx = qMax(mx, v);
    if (mx > 1e-6f) {
        for (float &v : peaks)
            v /= mx;
    }
    return peaks;
}

void MainWindow::setWaveformFromPcm(const QByteArray &pcm, const QAudioFormat &fmt)
{
    m_rawPeaks = WavFile::peaks(pcm, fmt, 400);
    if (m_rawPeaks.isEmpty()) {
        m_waveform->clear();
        return;
    }
    m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
}

void MainWindow::loadDocumentForPlayback(const QString &path)
{
    const WavFile::Info info = WavFile::load(path);
    if (!info.ok) {
        QMessageBox::warning(this, tr("Open"), info.error);
        m_waveform->clear();
        return;
    }
    m_player->loadPcm(info.pcm, info.format);
    m_player->clearPlayRange();
    setWaveformFromPcm(info.pcm, info.format);
    m_waveform->clearSelection();
}

void MainWindow::onNew()
{
    if (m_state == AppState::Recording)
        return;
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        if (m_player)
        m_player->stop();
    if (m_capture)
        m_capture->stop();
    if (!maybeSave())
        return;
    clearDocument();
}

void MainWindow::onOpen()
{
    if (m_state == AppState::Recording)
        return;
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        if (m_player)
        m_player->stop();
    if (m_capture)
        m_capture->stop();
    if (!maybeSave())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Audio File"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        tr("WAV Audio (*.wav);;All Files (*)"));
    if (path.isEmpty())
        return;

    clearDocument();
    m_savedPath = path;
    m_isTemporary = false;
    m_tempPath.clear();
    m_modified = false;
    updateWindowTitle();
    loadDocumentForPlayback(path);
    setAppState(AppState::Ready);
}

void MainWindow::onSave()
{
    if (!hasDocument())
        return;
    if (m_savedPath.isEmpty() || m_isTemporary || m_tempPath.startsWith(m_history.cacheDir())) {
        onSaveAs();
        return;
    }
    if (!m_tempPath.isEmpty() && m_tempPath != m_savedPath) {
        if (QFile::exists(m_savedPath))
            QFile::remove(m_savedPath);
        if (!QFile::copy(m_tempPath, m_savedPath)) {
            QMessageBox::critical(this, tr("Error"), tr("Could not save to:\n%1").arg(m_savedPath));
            return;
        }
        if (m_isTemporary)
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

    QString source = !m_tempPath.isEmpty() ? m_tempPath : m_savedPath;
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
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved"), 3000);
}

void MainWindow::onRecord()
{
    if (m_state == AppState::Recording) {
        // Leave capture running for the meter. Only disable the PCM queue
        // and flush whatever is already buffered — never sleep on the GUI thread.
        m_capture->setRecording(false);
        for (int i = 0; i < 3; ++i) {
            const QByteArray rest = m_capture->takeRecordedAudio();
            if (rest.isEmpty())
                break;
            if (m_wavWriter.isOpen())
                m_wavWriter.write(rest.constData(), rest.size());
        }
        m_wavWriter.close();
        m_recordAction->setChecked(false);

        if (!m_liveRecordPeaks.isEmpty()) {
            m_rawPeaks = m_liveRecordPeaks;
            m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
        }

        setAppState(AppState::Ready);
        m_monitoring = m_capture && m_capture->isRunning();

        if (!m_tempPath.isEmpty()) {
            QFileInfo fi(m_tempPath);
            if (fi.size() < 44) {
                QMessageBox::warning(this, tr("Recording"),
                    tr("Recording produced no audio data."));
                QFile::remove(m_tempPath);
                m_tempPath.clear();
            } else {
                const QString archived = m_history.archiveTake(m_tempPath);
                if (!archived.isEmpty()) {
                    if (m_isTemporary)
                        QFile::remove(m_tempPath);
                    m_tempPath = archived;
                    m_isTemporary = false;
                    m_modified = true;
                }
                // Load for playback (may take a moment on long takes — do not
                // processEvents here; that re-enters the meter timer).
                loadDocumentForPlayback(m_tempPath);
                statusBar()->showMessage(
                    tr("Take saved to cache (%1)").arg(m_history.takes().size()), 4000);
            }
        }
        return;
    }

    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();

    if (!m_tempPath.isEmpty() && m_isTemporary)
        QFile::remove(m_tempPath);

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
    m_savedPath.clear();

    // Keep the same capture stream — just enable PCM queue
    if (!m_monitoring || !m_capture->isRunning()) {
        stopMonitoring();
        m_capture->setGain(m_inputVolumeSlider->value() / 100.0);
        if (!m_capture->start(currentSourceName(), 48000, 1)) {
            QMessageBox::critical(this, tr("Error"), tr("Could not open PulseAudio source for recording."));
            m_recordAction->setChecked(false);
            return;
        }
        m_monitoring = true;
    }
    m_capture->setGain(m_inputVolumeSlider->value() / 100.0);

    QAudioFormat fmt;
    fmt.setSampleFormat(QAudioFormat::Int16);
    fmt.setSampleRate(48000);
    fmt.setChannelCount(1);
    if (!m_wavWriter.open(m_tempPath, fmt)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open WAV file for writing."));
        m_recordAction->setChecked(false);
        return;
    }

    m_liveRecordPeaks.clear();
    m_waveform->clear();
    m_recordTimer.restart();
    markModified();
    setAppState(AppState::Recording);
    m_recordAction->setChecked(true);
    m_capture->setRecording(true);
    if (m_history.takes().size() > 0)
        statusBar()->showMessage(
            tr("Recording… (previous takes remain in cache, %1 total)")
                .arg(m_history.takes().size()), 0);
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
            tr("Nothing to play. Record something or open a WAV file first."));
        m_playAction->setChecked(false);
        return;
    }

    {
        const WavFile::Info info = WavFile::load(path);
        if (!info.ok) {
            QMessageBox::warning(this, tr("Play"), info.error);
            m_playAction->setChecked(false);
            return;
        }
        m_player->loadPcm(info.pcm, info.format);
    }
    stopMonitoring();
    m_player->setSinkName(currentSinkName());
    m_player->play();
}

void MainWindow::onStop()
{
    if (m_state == AppState::Recording) {
        onRecord(); // stop recording
        return;
    }
    if (m_state == AppState::Playing || m_state == AppState::Paused) {
        if (m_player)
        m_player->stop();
    if (m_capture)
        m_capture->stop();
        m_seekSlider->setValue(0);
        m_waveform->setPlaybackPosition(0.0);
        updateTimeLabel();
    }
}

void MainWindow::onNormalize()
{
    if (m_state == AppState::Recording || m_state == AppState::Playing)
        return;
    if (!hasDocument()) {
        QMessageBox::information(this, tr("QWavRec"), tr("Nothing to normalize."));
        return;
    }
    if (normalizeCurrentFile()) {
        loadDocumentForPlayback(documentPathForPlayback());
        markModified();
        statusBar()->showMessage(tr("Normalized to peak"), 3000);
    } else {
        QMessageBox::warning(this, tr("Normalize"),
            tr("Could not normalize this file.\n"
               "Only 16-bit PCM WAV is supported."));
    }
}

bool MainWindow::normalizeCurrentFile()
{
    const QString path = documentPathForPlayback();
    if (path.isEmpty())
        return false;

    WavFile::Info info = WavFile::load(path);
    if (!info.ok || info.format.sampleFormat() != QAudioFormat::Int16)
        return false;

    if (!WavFile::peakNormalizeInt16(info.pcm))
        return false;

    // Rewrite file via WavWriter
    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("qwavrec-norm-XXXXXX.wav")));
    tmp.setAutoRemove(false);
    if (!tmp.open())
        return false;
    tmp.close();

    WavWriter writer;
    if (!writer.open(tmp.fileName(), info.format)) {
        QFile::remove(tmp.fileName());
        return false;
    }
    writer.write(info.pcm.constData(), info.pcm.size());
    writer.close();

    const QString archived = m_history.archiveTake(tmp.fileName());
    QFile::remove(tmp.fileName());
    if (archived.isEmpty())
        return false;

    m_tempPath = archived;
    m_isTemporary = false;
    m_savedPath.clear();
    return true;
}

void MainWindow::onLoopToggled(bool on)
{
    m_player->setLoop(on);
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::onAutoScaleWaveformToggled(bool on)
{
    m_autoScaleWaveform = on;
    if (!m_rawPeaks.isEmpty())
        m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::onUndo()
{
    if (m_state == AppState::Recording)
        return;
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        if (m_player)
        m_player->stop();
    if (m_capture)
        m_capture->stop();
    const QString path = m_history.previous();
    if (path.isEmpty())
        return;
    m_tempPath = path;
    m_isTemporary = false;
    m_savedPath.clear();
    m_modified = true;
    loadDocumentForPlayback(path);
    updateWindowTitle();
    setAppState(AppState::Ready);
    statusBar()->showMessage(
        tr("Take %1/%2").arg(m_history.currentIndex() + 1).arg(m_history.takes().size()), 3000);
}

void MainWindow::onRedo()
{
    if (m_state == AppState::Recording)
        return;
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        if (m_player)
        m_player->stop();
    if (m_capture)
        m_capture->stop();
    const QString path = m_history.next();
    if (path.isEmpty())
        return;
    m_tempPath = path;
    m_isTemporary = false;
    m_savedPath.clear();
    m_modified = true;
    loadDocumentForPlayback(path);
    updateWindowTitle();
    setAppState(AppState::Ready);
    statusBar()->showMessage(
        tr("Take %1/%2").arg(m_history.currentIndex() + 1).arg(m_history.takes().size()), 3000);
}

void MainWindow::onHistory()
{
    if (m_state == AppState::Recording)
        return;
    // Only stop playback — do not stop capture. pa_simple_read blocks, so
    // capture->stop() waiting on the worker freezes the GUI for seconds.
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();

    m_history.reload();

    HistoryDialog dlg(m_history.takes(), m_history.currentIndex(),
                      m_history.cacheDir(), this);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.adjustSize();
    if (auto *p = dlg.parentWidget()) {
        const QRect pg = p->geometry();
        dlg.move(pg.center() - dlg.rect().center());
    }
    dlg.show();
    dlg.raise();
    dlg.activateWindow();
    if (dlg.exec() != QDialog::Accepted)
        return;

    const int idx = dlg.selectedIndex();
    if (idx < 0)
        return;

    if (dlg.deleteRequested()) {
        const bool wasCurrent = (idx == m_history.currentIndex());
        if (!m_history.removeAt(idx))
            return;
        if (wasCurrent || m_history.takes().isEmpty()) {
            if (m_history.takes().isEmpty()) {
                clearDocument();
            } else {
                const QString path = m_history.currentPath();
                m_tempPath = path;
                m_isTemporary = false;
                m_savedPath.clear();
                m_modified = true;
                loadDocumentForPlayback(path);
                updateWindowTitle();
                setAppState(AppState::Ready);
            }
        }
        statusBar()->showMessage(tr("Take deleted"), 3000);
        return;
    }

    if (!m_history.selectIndex(idx))
        return;
    const QString path = m_history.currentPath();
    m_tempPath = path;
    m_isTemporary = false;
    m_savedPath.clear();
    m_modified = true;
    loadDocumentForPlayback(path);
    updateWindowTitle();
    setAppState(AppState::Ready);
    statusBar()->showMessage(
        tr("Take %1/%2").arg(m_history.currentIndex() + 1).arg(m_history.takes().size()), 3000);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About QWavRec"),
        tr("<h3>QWavRec</h3>"
           "<p>Simple WAV recorder/player using <b>PulseAudio</b> "
           "(sources including monitors, and sinks).</p>"
           "<p>Takes are archived under <code>~/.cache/qwavrec</code>.</p>"
           "<p>Version %1 · GPL-3.0-or-later</p>")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::refreshDevices()
{
    QString curIn = m_pendingInputName;
    QString curOut = m_pendingOutputName;
    if (curIn.isEmpty() && m_inputCombo->currentIndex() >= 0)
        curIn = m_inputCombo->currentData().toString();
    if (curOut.isEmpty() && m_outputCombo->currentIndex() >= 0)
        curOut = m_outputCombo->currentData().toString();

    // Single PulseAudio connection for sources + sinks + defaults
    const PulseDevices::Lists lists = PulseDevices::query();

    m_inputCombo->blockSignals(true);
    m_outputCombo->blockSignals(true);
    m_inputCombo->clear();
    int selIn = 0, i = 0;
    for (const PulseDevice &dev : lists.sources) {
        QString label = dev.description;
        if (dev.isMonitor)
            label += tr(" [monitor]");
        if (dev.isDefault)
            label += tr(" (default)");
        m_inputCombo->addItem(label, dev.name);
        if (dev.name == curIn || (curIn.isEmpty() && dev.isDefault))
            selIn = i;
        ++i;
    }
    if (m_inputCombo->count())
        m_inputCombo->setCurrentIndex(selIn);

    m_outputCombo->clear();
    int selOut = 0;
    i = 0;
    for (const PulseDevice &dev : lists.sinks) {
        QString label = dev.description;
        if (dev.isDefault)
            label += tr(" (default)");
        m_outputCombo->addItem(label, dev.name);
        if (dev.name == curOut || (curOut.isEmpty() && dev.isDefault))
            selOut = i;
        ++i;
    }
    if (m_outputCombo->count())
        m_outputCombo->setCurrentIndex(selOut);

    m_inputCombo->blockSignals(false);
    m_outputCombo->blockSignals(false);
    m_pendingInputName.clear();
    m_pendingOutputName.clear();

    if (m_inputCombo->count())
        onInputDeviceChanged(m_inputCombo->currentIndex());
    if (m_outputCombo->count())
        onOutputDeviceChanged(m_outputCombo->currentIndex());
}

void MainWindow::onInputDeviceChanged(int index)
{
    Q_UNUSED(index);
    if (m_state == AppState::Recording)
        onRecord();
    if (m_state == AppState::Ready || m_state == AppState::Error)
        startMonitoring();
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::onOutputDeviceChanged(int index)
{
    Q_UNUSED(index);
    if (m_player)
        m_player->setSinkName(currentSinkName());
    if (!m_restoringSettings)
        saveSettings();
}

QString MainWindow::currentSourceName() const
{
    if (m_inputCombo->currentIndex() < 0)
        return {};
    return m_inputCombo->currentData().toString();
}

QString MainWindow::currentSinkName() const
{
    if (m_outputCombo->currentIndex() < 0)
        return {};
    return m_outputCombo->currentData().toString();
}

void MainWindow::onInputVolumeChanged(int value)
{
    if (m_capture)
        m_capture->setGain(value / 100.0);
    updateMicGainLabel();
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::onOutputVolumeChanged(int value)
{
    m_player->setVolume(value / 100.0);
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::updateMicGainLabel()
{
    const int v = m_inputVolumeSlider->value();
    if (v > 100)
        m_micGainLabel->setText(tr("%1% ↑").arg(v));
    else
        m_micGainLabel->setText(tr("%1%").arg(v));
    m_micGainLabel->setStyleSheet(v > 100
        ? QStringLiteral("color: #c0392b; font-weight: bold;")
        : QString());
}

void MainWindow::startMonitoring()
{
    if (m_state == AppState::Recording)
        return;
    if (!m_capture)
        return;
    // Avoid thrashing: if already running on the same device, keep it
    if (m_monitoring && m_capture->isRunning()) {
        m_capture->setGain(m_inputVolumeSlider->value() / 100.0);
        return;
    }
    stopMonitoring();
    m_capture->setGain(m_inputVolumeSlider->value() / 100.0);
    m_capture->setRecording(false);
    if (!m_capture->start(currentSourceName(), 48000, 1)) {
        statusBar()->showMessage(tr("Could not open capture device"), 3000);
        return;
    }
    m_monitoring = true;
}

void MainWindow::stopMonitoring()
{
    if (m_capture) {
        m_capture->setRecording(false);
        m_capture->stop();
    }
    m_monitoring = false;
    if (m_inputMeter)
        m_inputMeter->setLevel(0.0);
}

void MainWindow::onCaptureError(const QString &msg)
{
    QMessageBox::critical(this, tr("Capture Error"), msg);
    if (m_state == AppState::Recording)
        onRecord();
}

void MainWindow::onMeterTick()
{
    if (!m_capture)
        return;

    // Input meter from lock-free peak (no per-buffer GUI events)
    if (m_monitoring || m_state == AppState::Recording)
        m_inputMeter->setLevel(m_capture->currentPeak());
    else
        m_inputMeter->setLevel(0.0);

    // Drain PCM while recording
    if (m_state == AppState::Recording && m_wavWriter.isOpen()) {
        const QByteArray pcm = m_capture->takeRecordedAudio();
        if (!pcm.isEmpty()) {
            m_wavWriter.write(pcm.constData(), pcm.size());
            // Peak for live waveform (downsample: one sample per tick)
            float peak = 0.f;
            const auto *s = reinterpret_cast<const qint16 *>(pcm.constData());
            const int n = pcm.size() / 2;
            for (int i = 0; i < n; i += 8) // stride — cheap
                peak = qMax(peak, qAbs(s[i] / 32768.f));
            m_liveRecordPeaks.append(peak);
            if (m_liveRecordPeaks.size() > 800) {
                QVector<float> reduced;
                reduced.reserve(400);
                for (int i = 0; i + 1 < m_liveRecordPeaks.size(); i += 2)
                    reduced.append(qMax(m_liveRecordPeaks[i], m_liveRecordPeaks[i + 1]));
                m_liveRecordPeaks = reduced;
            }
            m_rawPeaks = m_liveRecordPeaks;
            m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
        }
        m_timeLabel->setText(formatTime(m_recordTimer.elapsed()));
        m_duration = m_recordTimer.elapsed();
    }

    // Output meter while playing
    if (m_state == AppState::Playing && m_player && m_duration > 0) {
        const qreal vol = m_outputVolumeSlider->value() / 100.0;
        m_outputMeter->setLevel(m_player->levelAtPosition(m_player->position()) * vol);
    }
}

void MainWindow::onWaveformSeek(qreal pos)
{
    if (!hasDocument() || m_duration <= 0 || m_state == AppState::Recording)
        return;
    const int ms = int(pos * m_duration);
    m_seekSlider->setValue(ms);
    onSeek(ms);
}

void MainWindow::onSelectionChanged(qreal start, qreal end)
{
    applySelectionToPlayer();
    if (end > start + 1e-6 && m_duration > 0) {
        statusBar()->showMessage(
            tr("Selection A–B: %1 – %2")
                .arg(formatTime(qint64(start * m_duration)))
                .arg(formatTime(qint64(end * m_duration))),
            0);
    } else {
        statusBar()->showMessage(tr("Selection cleared"), 2000);
    }
}

void MainWindow::applySelectionToPlayer()
{
    if (!m_player || m_duration <= 0)
        return;
    if (m_waveform->hasSelection()) {
        const qint64 a = qint64(m_waveform->selectionStart() * m_duration);
        const qint64 b = qint64(m_waveform->selectionEnd() * m_duration);
        m_player->setPlayRange(a, b);
    } else {
        m_player->clearPlayRange();
    }
}

void MainWindow::onPlayerStateChanged(PulsePlayback::State state)
{
    switch (state) {
    case PulsePlayback::Playing:
        setAppState(AppState::Playing);
        m_playAction->setChecked(true);
        m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-pause"), QStyle::SP_MediaPause));
        break;
    case PulsePlayback::Paused:
        setAppState(AppState::Paused);
        m_playAction->setChecked(true);
        m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
        break;
    case PulsePlayback::Stopped:
        if (m_state != AppState::Recording) {
            setAppState(AppState::Ready);
            m_playAction->setChecked(false);
            m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
            m_seekSlider->setValue(0);
            m_waveform->setPlaybackPosition(0.0);
            updateTimeLabel();
            m_outputMeter->setLevel(0.0);
            startMonitoring();
        }
        break;
    }
}

void MainWindow::onPlayerPosition(qint64 ms)
{
    if (!m_seeking && (m_state == AppState::Playing || m_state == AppState::Paused)) {
        m_seekSlider->blockSignals(true);
        m_seekSlider->setValue(static_cast<int>(ms));
        m_seekSlider->blockSignals(false);
        updateTimeLabel();
        if (m_duration > 0)
            m_waveform->setPlaybackPosition(static_cast<qreal>(ms) / m_duration);
        if (m_state == AppState::Playing) {
            const qreal vol = m_outputVolumeSlider->value() / 100.0;
            m_outputMeter->setLevel(m_player->levelAtPosition(ms) * vol);
        }
    }
}

void MainWindow::onPlayerDuration(qint64 ms)
{
    m_duration = ms;
    m_seekSlider->setRange(0, static_cast<int>(ms));
    updateTimeLabel();
    applySelectionToPlayer();
}

void MainWindow::onPlayerError(const QString &msg)
{
    QMessageBox::critical(this, tr("Playback Error"), msg);
    m_playAction->setChecked(false);
    setAppState(AppState::Ready);
}

void MainWindow::onSeek(int value)
{
    m_player->setPosition(value);
    if (m_duration > 0)
        m_waveform->setPlaybackPosition(static_cast<qreal>(value) / m_duration);
    updateTimeLabel();
}

void MainWindow::setAppState(AppState state)
{
    m_state = state;
    updateControls();
    switch (state) {
    case AppState::Ready: statusBar()->showMessage(tr("Ready")); break;
    case AppState::Playing: statusBar()->showMessage(tr("Playing")); break;
    case AppState::Paused: statusBar()->showMessage(tr("Paused")); break;
    case AppState::Recording: statusBar()->showMessage(tr("Recording…")); break;
    case AppState::Error: statusBar()->showMessage(tr("Error")); break;
    }
}

void MainWindow::updateControls()
{
    const bool ready = (m_state == AppState::Ready || m_state == AppState::Error);
    const bool playing = (m_state == AppState::Playing || m_state == AppState::Paused);
    const bool recording = (m_state == AppState::Recording);
    const bool hasDoc = hasDocument();

    // New/Open allowed whenever not recording (including while paused)
    m_newAction->setEnabled(!recording);
    m_openAction->setEnabled(!recording);
    m_saveAction->setEnabled(!recording && (m_modified || hasDoc));
    m_saveAsAction->setEnabled(!recording && hasDoc);
    m_undoAction->setEnabled(ready && m_history.canPrevious());
    m_redoAction->setEnabled(ready && m_history.canNext());
    m_historyAction->setEnabled(ready);
    m_normalizeAction->setEnabled(ready && hasDoc);
    m_recordAction->setEnabled(ready || recording);
    m_playAction->setEnabled((ready || playing) && hasDoc);
    m_stopAction->setEnabled(playing || recording);
    m_inputCombo->setEnabled(ready);
    m_outputCombo->setEnabled(!recording);
    // Seek whenever a document is loaded (scrub before play)
    m_seekSlider->setEnabled(hasDoc && !recording && m_duration > 0);
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
    QString name;
    if (!m_savedPath.isEmpty()) {
        name = QFileInfo(m_savedPath).fileName();
    } else if (!m_tempPath.isEmpty()) {
        if (m_history.takes().size() > 0 && m_history.currentIndex() >= 0)
            name = tr("Take %1/%2 (cache)").arg(m_history.currentIndex() + 1).arg(m_history.takes().size());
        else
            name = tr("Unexported take");
    } else {
        name = tr("Untitled");
    }
    if (m_modified && m_savedPath.isEmpty())
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

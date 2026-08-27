// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "levelmeter.h"
#include "waveformwidget.h"
#include "markedslider.h"
#include "recordinghistory.h"

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
#include <QAudioDevice>
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
#include <QtEndian>

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

    m_player = new WavPlayer(this);
    connect(m_player, &WavPlayer::stateChanged, this, &MainWindow::onPlayerStateChanged);
    connect(m_player, &WavPlayer::positionChanged, this, &MainWindow::onPlayerPosition);
    connect(m_player, &WavPlayer::durationChanged, this, &MainWindow::onPlayerDuration);
    connect(m_player, &WavPlayer::errorOccurred, this, &MainWindow::onPlayerError);

    connect(&m_devices, &QMediaDevices::audioInputsChanged, this, &MainWindow::updateAudioDevices);
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, &MainWindow::updateAudioDevices);
    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputDeviceChanged);
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);
    connect(m_inputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onInputVolumeChanged);
    connect(m_outputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onOutputVolumeChanged);

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

    loadSettings();
    updateAudioDevices();
    setAppState(AppState::Ready);
    startAudioSource();
    updateWindowTitle();
}

MainWindow::~MainWindow()
{
    saveSettings();
    if (m_state == AppState::Recording)
        onRecord();
    stopAudioSource();
    m_player->stop();
    if (!m_tempPath.isEmpty() && m_isTemporary)
        QFile::remove(m_tempPath);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    if (maybeSave())
        event->accept();
    else
        event->ignore();
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

    m_undoAction = new QAction(themeIcon(QStringLiteral("edit-undo"), QStyle::SP_ArrowBack),
                               tr("&Undo Take"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::onUndo);

    m_redoAction = new QAction(themeIcon(QStringLiteral("edit-redo"), QStyle::SP_ArrowForward),
                               tr("&Redo Take"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::onRedo);

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
    fileMenu->addAction(m_undoAction);
    fileMenu->addAction(m_redoAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_normalizeAction);

    QMenu *transportMenu = menuBar()->addMenu(tr("&Transport"));
    transportMenu->addAction(m_recordAction);
    transportMenu->addAction(m_playAction);
    transportMenu->addAction(m_stopAction);
    transportMenu->addSeparator();
    transportMenu->addAction(m_loopAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
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
    m_pendingInputId = s.value(QStringLiteral("audio/inputId")).toString();
    m_pendingOutputId = s.value(QStringLiteral("audio/outputId")).toString();
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
        s.setValue(QStringLiteral("audio/inputId"), m_inputCombo->currentData().toByteArray());
    if (m_outputCombo->currentIndex() >= 0)
        s.setValue(QStringLiteral("audio/outputId"), m_outputCombo->currentData().toByteArray());
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
    if (!m_modified)
        return true;
    const auto ret = QMessageBox::warning(
        this, tr("QWavRec"),
        tr("The recording has not been saved.\nDo you want to save it?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (ret == QMessageBox::Save) {
        onSave();
        return !m_modified;
    }
    if (ret == QMessageBox::Cancel)
        return false;
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
    if (pcm.isEmpty() || fmt.sampleRate() <= 0) {
        m_waveform->clear();
        return;
    }
    const int bpf = fmt.bytesPerFrame();
    if (bpf <= 0) {
        m_waveform->clear();
        return;
    }
    const int frames = pcm.size() / bpf;
    const int target = 400;
    QVector<float> peaks;
    peaks.reserve(target);
    const double step = double(qMax(1, frames)) / target;
    for (int i = 0; i < target; ++i) {
        const int a = int(i * step);
        const int b = qMin(int((i + 1) * step), frames);
        float mx = 0.f;
        for (int f = a; f < b; ++f) {
            const char *frame = pcm.constData() + f * bpf;
            float sample = 0.f;
            switch (fmt.sampleFormat()) {
            case QAudioFormat::Int16:
                sample = qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(frame)) / 32768.f;
                break;
            case QAudioFormat::Float:
                sample = *reinterpret_cast<const float *>(frame);
                break;
            case QAudioFormat::UInt8:
                sample = (quint8(frame[0]) - 128) / 128.f;
                break;
            case QAudioFormat::Int32:
                sample = qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(frame)) / 2147483648.f;
                break;
            default:
                break;
            }
            mx = qMax(mx, qAbs(sample));
        }
        peaks.append(mx);
    }
    m_rawPeaks = peaks;
    m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
}

void MainWindow::loadDocumentForPlayback(const QString &path)
{
    if (!m_player->load(path))
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    file.read(12);
    qint64 dataOff = -1;
    quint32 dataSz = 0;
    QAudioFormat fmt;
    while (!file.atEnd()) {
        const QByteArray id = file.read(4);
        const QByteArray szb = file.read(4);
        if (id.size() < 4 || szb.size() < 4) break;
        const quint32 sz = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(szb.constData()));
        const qint64 pos = file.pos();
        if (id == "fmt ") {
            QByteArray fmtb = file.read(qMin(sz, 32u));
            if (fmtb.size() >= 16) {
                const quint16 af = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmtb.constData()));
                const quint16 ch = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmtb.constData() + 2));
                const quint32 sr = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(fmtb.constData() + 4));
                const quint16 bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmtb.constData() + 14));
                fmt.setChannelCount(ch);
                fmt.setSampleRate(int(sr));
                if (af == 1 && bits == 16) fmt.setSampleFormat(QAudioFormat::Int16);
                else if (af == 3 && bits == 32) fmt.setSampleFormat(QAudioFormat::Float);
                else if (af == 1 && bits == 8) fmt.setSampleFormat(QAudioFormat::UInt8);
                else if (af == 1 && bits == 32) fmt.setSampleFormat(QAudioFormat::Int32);
            }
            if (sz > 32) file.seek(pos + sz + (sz & 1));
        } else if (id == "data") {
            dataOff = pos;
            dataSz = sz;
            break;
        } else {
            file.seek(pos + sz + (sz & 1));
        }
    }
    if (dataOff >= 0) {
        file.seek(dataOff);
        setWaveformFromPcm(file.read(dataSz), fmt);
    }
}

void MainWindow::onNew()
{
    if (m_state == AppState::Recording)
        return;
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();
    if (!maybeSave())
        return;
    clearDocument();
}

void MainWindow::onOpen()
{
    if (m_state == AppState::Recording)
        return;
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();
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
        m_wavWriter.close();
        m_recordAction->setChecked(false);

        if (!m_liveRecordPeaks.isEmpty()) {
            m_rawPeaks = m_liveRecordPeaks;
            m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
        }

        if (!m_tempPath.isEmpty()) {
            const QString archived = m_history.archiveTake(m_tempPath);
            if (!archived.isEmpty()) {
                if (m_isTemporary)
                    QFile::remove(m_tempPath);
                m_tempPath = archived;
                m_isTemporary = false;
                m_modified = true;
            }
            loadDocumentForPlayback(m_tempPath);
            statusBar()->showMessage(
                tr("Take saved to cache (%1)").arg(m_history.takes().size()), 4000);
        }
        setAppState(AppState::Ready);
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

    if (!m_audioSource)
        startAudioSource();
    if (!m_audioSource) {
        QMessageBox::critical(this, tr("Error"), tr("No audio input available."));
        m_recordAction->setChecked(false);
        return;
    }

    const QAudioFormat fmt = m_audioSource->format();
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

    if (m_player->path() != path) {
        if (!m_player->load(path)) {
            m_playAction->setChecked(false);
            return;
        }
    }
    m_player->play();
}

void MainWindow::onStop()
{
    if (m_state == AppState::Recording) {
        onRecord(); // stop recording
        return;
    }
    if (m_state == AppState::Playing || m_state == AppState::Paused) {
        m_player->stop();
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
    }
}

bool MainWindow::normalizeCurrentFile()
{
    const QString path = documentPathForPlayback();
    if (path.isEmpty())
        return false;

    // Load via player to get PCM, or re-parse
    if (m_player->path() != path && !m_player->load(path))
        return false;

    // Re-read file and rewrite with gain
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray all = in.readAll();
    in.close();
    if (all.size() < 44)
        return false;

    // Find data chunk
    int dataOff = -1;
    quint32 dataSz = 0;
    QAudioFormat fmt;
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        file.read(12);
        while (!file.atEnd()) {
            const QByteArray id = file.read(4);
            const QByteArray szb = file.read(4);
            if (id.size() < 4 || szb.size() < 4) break;
            const quint32 sz = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(szb.constData()));
            const qint64 pos = file.pos();
            if (id == "fmt ") {
                QByteArray fmtb = file.read(qMin(sz, 32u));
                if (fmtb.size() >= 16) {
                    const quint16 af = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmtb.constData()));
                    const quint16 ch = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmtb.constData() + 2));
                    const quint32 sr = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(fmtb.constData() + 4));
                    const quint16 bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(fmtb.constData() + 14));
                    fmt.setChannelCount(ch);
                    fmt.setSampleRate(int(sr));
                    if (af == 1 && bits == 16) fmt.setSampleFormat(QAudioFormat::Int16);
                    else if (af == 3) fmt.setSampleFormat(QAudioFormat::Float);
                    else return false;
                }
                if (sz > 32) file.seek(pos + sz + (sz & 1));
            } else if (id == "data") {
                dataOff = int(pos);
                dataSz = sz;
                break;
            } else {
                file.seek(pos + sz + (sz & 1));
            }
        }
    }
    if (dataOff < 0 || dataSz == 0 || fmt.sampleFormat() != QAudioFormat::Int16)
        return false;

    QByteArray pcm = all.mid(dataOff, int(dataSz));
    auto *s = reinterpret_cast<qint16 *>(pcm.data());
    const int n = pcm.size() / 2;
    int peak = 0;
    for (int i = 0; i < n; ++i)
        peak = qMax(peak, qAbs(int(s[i])));
    if (peak <= 0 || peak >= 32767)
        return true; // already maxed or silent

    const double gain = 32767.0 / peak;
    for (int i = 0; i < n; ++i) {
        const int v = int(qRound(s[i] * gain));
        s[i] = qint16(qBound(-32768, v, 32767));
    }

    QByteArray out = all;
    out.replace(dataOff, int(dataSz), pcm);

    // Write to a new temp, then archive / replace
    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("qwavrec-norm-XXXXXX.wav")));
    tmp.setAutoRemove(false);
    if (!tmp.open())
        return false;
    tmp.write(out);
    tmp.close();

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
        m_player->stop();
    const QString path = m_history.undo();
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
        m_player->stop();
    const QString path = m_history.redo();
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

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About QWavRec"),
        tr("<h3>QWavRec</h3>"
           "<p>Simple WAV recorder/player (QAudioSource / QAudioSink).</p>"
           "<p>Takes are archived under <code>~/.cache/qwavrec</code>. "
           "Undo/Redo steps through them. Normalize peak-scales without clipping.</p>"
           "<p>Version %1 · GPL-3.0-or-later</p>")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::updateAudioDevices()
{
    QByteArray curIn = m_pendingInputId.toUtf8();
    QByteArray curOut = m_pendingOutputId.toUtf8();
    if (curIn.isEmpty() && m_inputCombo->currentIndex() >= 0)
        curIn = m_inputCombo->currentData().toByteArray();
    if (curOut.isEmpty() && m_outputCombo->currentIndex() >= 0)
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
    m_pendingInputId.clear();
    m_pendingOutputId.clear();
}

void MainWindow::onInputDeviceChanged(int index)
{
    Q_UNUSED(index);
    if (m_state == AppState::Recording)
        onRecord();
    startAudioSource();
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::onOutputDeviceChanged(int index)
{
    if (index < 0) return;
    const QByteArray id = m_outputCombo->itemData(index).toByteArray();
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        if (dev.id() == id) {
            m_player->setDevice(dev);
            break;
        }
    }
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::onInputVolumeChanged(int value)
{
    m_micGain = value / 100.0;
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

void MainWindow::startAudioSource()
{
    stopAudioSource();
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

    QAudioFormat format;
    format.setSampleFormat(QAudioFormat::Int16);
    format.setSampleRate(48000);
    format.setChannelCount(1);
    if (!device.isFormatSupported(format)) {
        format = device.preferredFormat();
        QAudioFormat try16 = format;
        try16.setSampleFormat(QAudioFormat::Int16);
        if (device.isFormatSupported(try16))
            format = try16;
    }

    m_audioSource = new QAudioSource(device, format, this);
    m_audioSourceDevice = m_audioSource->start();
    if (m_audioSourceDevice)
        connect(m_audioSourceDevice, &QIODevice::readyRead, this, &MainWindow::onAudioSourceReadyRead);
}

void MainWindow::stopAudioSource()
{
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        m_audioSourceDevice = nullptr;
    }
}

float MainWindow::processCaptureBuffer(QByteArray &data, const QAudioFormat &fmt)
{
    float peak = 0.f;
    if (fmt.sampleFormat() == QAudioFormat::Int16) {
        auto *s = reinterpret_cast<qint16 *>(data.data());
        const int n = data.size() / int(sizeof(qint16));
        for (int i = 0; i < n; ++i) {
            float v = s[i] / 32768.f * float(m_micGain);
            v = qBound(-1.f, v, 1.f);
            peak = qMax(peak, qAbs(v));
            s[i] = qint16(v * 32767.f);
        }
    } else if (fmt.sampleFormat() == QAudioFormat::Float) {
        auto *s = reinterpret_cast<float *>(data.data());
        const int n = data.size() / int(sizeof(float));
        for (int i = 0; i < n; ++i) {
            float v = qBound(-1.f, s[i] * float(m_micGain), 1.f);
            peak = qMax(peak, qAbs(v));
            s[i] = v;
        }
    }
    return peak;
}

void MainWindow::onAudioSourceReadyRead()
{
    if (!m_audioSourceDevice || !m_audioSource)
        return;
    QByteArray data = m_audioSourceDevice->readAll();
    if (data.isEmpty())
        return;

    const QAudioFormat fmt = m_audioSource->format();
    const float peak = processCaptureBuffer(data, fmt);
    m_inputMeter->setLevel(qreal(peak));

    if (m_state == AppState::Recording && m_wavWriter.isOpen()) {
        m_wavWriter.write(data.constData(), data.size());
        m_liveRecordPeaks.append(peak);
        if (m_liveRecordPeaks.size() > 800) {
            QVector<float> reduced;
            reduced.reserve(400);
            for (int i = 0; i < 400; ++i)
                reduced.append(qMax(m_liveRecordPeaks[i * 2], m_liveRecordPeaks[i * 2 + 1]));
            m_liveRecordPeaks = reduced;
        }
        m_rawPeaks = m_liveRecordPeaks;
        m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
        m_timeLabel->setText(formatTime(m_recordTimer.elapsed()));
        m_duration = m_recordTimer.elapsed();
    }
}

void MainWindow::onPlayerStateChanged(WavPlayer::State state)
{
    switch (state) {
    case WavPlayer::Playing:
        setAppState(AppState::Playing);
        m_playAction->setChecked(true);
        m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-pause"), QStyle::SP_MediaPause));
        break;
    case WavPlayer::Paused:
        setAppState(AppState::Paused);
        m_playAction->setChecked(true);
        m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
        break;
    case WavPlayer::Stopped:
        if (m_state != AppState::Recording) {
            setAppState(AppState::Ready);
            m_playAction->setChecked(false);
            m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
            m_seekSlider->setValue(0);
            m_waveform->setPlaybackPosition(0.0);
            updateTimeLabel();
            m_outputMeter->setLevel(0.0);
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
        if (m_state == AppState::Playing)
            m_outputMeter->setLevel(m_player->levelAtPosition(ms));
    }
}

void MainWindow::onPlayerDuration(qint64 ms)
{
    m_duration = ms;
    m_seekSlider->setRange(0, static_cast<int>(ms));
    updateTimeLabel();
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
    m_undoAction->setEnabled(ready && m_history.canUndo());
    m_redoAction->setEnabled(ready && m_history.canRedo());
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
            name = tr("Take %1/%2").arg(m_history.currentIndex() + 1).arg(m_history.takes().size());
        else
            name = QFileInfo(m_tempPath).fileName();
    } else {
        name = tr("Untitled");
    }
    if (m_modified)
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

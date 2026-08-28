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
#include <QSizePolicy>
#include <QComboBox>
#include <QAbstractItemView>
#include <QLabel>
#include <QSlider>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QToolButton>
#include <QStatusBar>
#include <QDockWidget>
#include <QListWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QStandardPaths>
#include <QDir>
#include <QList>
#include <QFileInfo>
#include <QFile>
#include <QAudioFormat>
#include <QImage>
#include <QStyle>
#include <QIcon>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QPixmap>
#include <QKeySequence>
#include <QCloseEvent>
#include <QTemporaryFile>
#include <QSettings>
#include <QtMath>
#include <algorithm>

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

    m_takesPanel = new TakesPanel(this);
    m_takesPanel->setCacheDir(m_history.cacheDir());
    m_takesDock = new QDockWidget(tr("Takes"), this);
    m_takesDock->setObjectName(QStringLiteral("TakesDock"));
    m_takesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_takesDock->setWidget(m_takesPanel);
    m_takesDock->setMinimumWidth(220);
    addDockWidget(Qt::RightDockWidgetArea, m_takesDock);
    m_takesDock->hide();
    connect(m_takesDock, &QDockWidget::visibilityChanged, this, [this](bool vis) {
        if (m_historyAction)
            m_historyAction->setChecked(vis);
        if (vis)
            refreshTakesPanel();
    });
    connect(m_takesPanel, &TakesPanel::loadRequested, this, &MainWindow::onTakesLoadRequested);
    connect(m_takesPanel, &TakesPanel::deleteRequested, this, &MainWindow::onTakesDeleteRequested);

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

    // Event-driven device list: long-lived Pulse context on a worker thread
    // (subscribe to source/sink/server). Debounce storms before touching combos.
    m_deviceWatcher = new PulseDeviceWatcher(this);
    m_deviceDebounceTimer = new QTimer(this);
    m_deviceDebounceTimer->setSingleShot(true);
    m_deviceDebounceTimer->setInterval(100);
    connect(m_deviceDebounceTimer, &QTimer::timeout, this, &MainWindow::refreshDevices);
    connect(m_deviceWatcher, &PulseDeviceWatcher::devicesChanged,
            this, &MainWindow::onPulseDevicesChanged, Qt::QueuedConnection);
    m_deviceWatcher->start();

    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputDeviceChanged);
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);
    connect(m_waveform, &WaveformWidget::seekRequested, this, &MainWindow::onWaveformSeek);
    connect(m_waveform, &WaveformWidget::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_waveform, &WaveformWidget::contextMenuRequested, this, &MainWindow::onWaveformContextMenu);
    connect(m_waveform, &WaveformWidget::contentWidthChanged, this, &MainWindow::onWaveformWidthChanged);
    connect(m_inputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onInputVolumeChanged);
    connect(m_outputVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onOutputVolumeChanged);

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_seeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        onSeek(m_seekSlider->value());
    });
    // Absolute groove click (SeekSlider) and keyboard changes: seek immediately.
    // Player position updates use blockSignals, so they do not re-enter onSeek.
    connect(m_seekSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_seeking || m_state == AppState::Recording)
            return;
        onSeek(v);
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
    if (m_deviceWatcher)
        m_deviceWatcher->stop();
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
                                  tr("Take &History"), this);
    m_historyAction->setStatusTip(tr("Show or hide the take history panel"));
    m_historyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    m_historyAction->setCheckable(true);
    connect(m_historyAction, &QAction::toggled, this, &MainWindow::onHistory);

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
    m_stopAction->setStatusTip(tr("Stop playback or recording and return to the start"));
    m_stopAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);

    m_normalizeAction = new QAction(themeIcon(QStringLiteral("audio-volume-high"), QStyle::SP_MediaVolume),
                                    tr("&Normalize"), this);
    m_normalizeAction->setStatusTip(tr("Peak-normalize the current recording without clipping"));
    connect(m_normalizeAction, &QAction::triggered, this, &MainWindow::onNormalize);

    m_cutAction = new QAction(themeIcon(QStringLiteral("edit-cut"), QStyle::SP_FileDialogContentsView),
                              tr("Cu&t"), this);
    m_cutAction->setShortcut(QKeySequence::Cut);
    m_cutAction->setStatusTip(tr("Cut the A–B selection to the clipboard"));
    connect(m_cutAction, &QAction::triggered, this, &MainWindow::onCut);

    m_copyAction = new QAction(themeIcon(QStringLiteral("edit-copy"), QStyle::SP_FileDialogDetailedView),
                               tr("&Copy"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_copyAction->setStatusTip(tr("Copy the A–B selection to the clipboard"));
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::onCopy);

    m_pasteAction = new QAction(themeIcon(QStringLiteral("edit-paste"), QStyle::SP_DialogOpenButton),
                                tr("&Paste"), this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    m_pasteAction->setStatusTip(tr("Paste clipboard audio at the playhead (or replace selection)"));
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::onPaste);

    m_deleteSelAction = new QAction(tr("&Delete Selection"), this);
    m_deleteSelAction->setShortcut(QKeySequence::Delete);
    m_deleteSelAction->setStatusTip(tr("Remove the A–B region"));
    connect(m_deleteSelAction, &QAction::triggered, this, &MainWindow::onDeleteSelection);

    m_cropAction = new QAction(tr("Crop to &Selection"), this);
    m_cropAction->setStatusTip(tr("Keep only the A–B region (trim outside)"));
    connect(m_cropAction, &QAction::triggered, this, &MainWindow::onCropToSelection);

    m_editUndoAction = new QAction(themeIcon(QStringLiteral("edit-undo"), QStyle::SP_ArrowBack),
                                   tr("&Undo"), this);
    m_editUndoAction->setShortcut(QKeySequence::Undo);
    m_editUndoAction->setStatusTip(tr("Undo the last edit"));
    connect(m_editUndoAction, &QAction::triggered, this, &MainWindow::onEditUndo);

    m_editRedoAction = new QAction(themeIcon(QStringLiteral("edit-redo"), QStyle::SP_ArrowForward),
                                   tr("&Redo"), this);
    m_editRedoAction->setShortcut(QKeySequence::Redo);
    m_editRedoAction->setStatusTip(tr("Redo the last undone edit"));
    connect(m_editRedoAction, &QAction::triggered, this, &MainWindow::onEditRedo);

    {
        // Distinct from Record: red bar with a vertical insert mark
        QPixmap pix(48, 48);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(40, 40, 40));
        painter.setPen(QPen(QColor(20, 20, 20), 2));
        painter.drawRoundedRect(6, 16, 36, 16, 3, 3);
        painter.setBrush(QColor(220, 50, 50));
        painter.setPen(Qt::NoPen);
        painter.drawRect(18, 8, 12, 32); // insert block
        painter.setPen(QPen(QColor(255, 220, 60), 3));
        painter.drawLine(24, 4, 24, 44); // playhead mark
        m_insertRecordAction = new QAction(QIcon(pix), tr("&Insert"), this);
    }
    m_insertRecordAction->setCheckable(true);
    m_insertRecordAction->setStatusTip(
        tr("Insert mode: recording is spliced into the current document at the playhead "
           "(instead of starting a new take)"));
    m_insertRecordAction->setToolTip(tr("Insert at playhead"));
    connect(m_insertRecordAction, &QAction::toggled, this, &MainWindow::onInsertRecordToggled);


    m_loopAction = new QAction(themeIcon(QStringLiteral("media-playlist-repeat"), QStyle::SP_BrowserReload),
                               tr("Loop"), this);
    m_loopAction->setStatusTip(tr("Repeat playback"));
    m_loopAction->setCheckable(true);
    connect(m_loopAction, &QAction::toggled, this, &MainWindow::onLoopToggled);

    m_autoScaleAction = new QAction(tr("Auto-&Scale Waveform"), this);
    m_autoScaleAction->setStatusTip(tr("Normalize waveform display so quiet recordings fill the view"));
    m_autoScaleAction->setCheckable(true);
    connect(m_autoScaleAction, &QAction::toggled, this, &MainWindow::onAutoScaleWaveformToggled);

    m_stereoAction = new QAction(tr("&Stereo Capture"), this);
    m_stereoAction->setStatusTip(tr("Record two channels when the input source supports stereo (e.g. sink monitors)"));
    m_stereoAction->setCheckable(true);
    m_stereoAction->setChecked(true);
    m_stereoAction->setToolTip(tr("Stereo capture"));
    connect(m_stereoAction, &QAction::toggled, this, &MainWindow::onStereoToggled);

    // Exclusive view modes (toolbar + View menu). Qt has no QRadioButton on
    // toolbars; the proper pattern is checkable QActions in a QActionGroup.
    // Prefer freedesktop/theme icons when the icon theme provides them; otherwise
    // paint a small fallback (always visible regardless of theme).
    auto themeOrFallback = [](const QStringList &names, const QIcon &fallback) {
        for (const QString &name : names) {
            const QIcon icon = QIcon::fromTheme(name);
            if (!icon.isNull())
                return icon;
        }
        return fallback;
    };
    auto paintWaveformIcon = []() {
        QPixmap pix(24, 24);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(40, 180, 90), 2.0));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(2, 12);
        path.cubicTo(6, 4, 8, 20, 12, 12);
        path.cubicTo(16, 4, 18, 20, 22, 12);
        p.drawPath(path);
        p.setPen(QPen(QColor(40, 180, 90, 90), 1));
        p.drawLine(2, 12, 22, 12);
        return QIcon(pix);
    };
    auto paintSpectrogramIcon = []() {
        QPixmap pix(24, 24);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, false);
        const QColor cols[] = {
            QColor(30, 40, 90), QColor(20, 120, 160),
            QColor(40, 180, 90), QColor(220, 200, 50), QColor(230, 80, 40)
        };
        for (int row = 0; row < 5; ++row) {
            p.setBrush(cols[row]);
            p.setPen(Qt::NoPen);
            for (int col = 0; col < 6; ++col) {
                const int h = 2 + ((row + col) % 3);
                p.drawRect(3 + col * 3, 20 - row * 3 - h, 2, h);
            }
        }
        return QIcon(pix);
    };

    // Waveform: line chart / XY curve (common in Breeze, Adwaita, etc.)
    const QIcon waveIcon = themeOrFallback(
        {QStringLiteral("office-chart-line"),
         QStringLiteral("office-chart-line-stacked"),
         QStringLiteral("labplot-xy-curve"),
         QStringLiteral("draw-bezier-curves"),
         QStringLiteral("audio-x-generic")},
        paintWaveformIcon());
    // Spectrogram: no universal name; histogram/area charts are the closest.
    const QIcon specIcon = themeOrFallback(
        {QStringLiteral("view-object-histogram-linear"),
         QStringLiteral("office-chart-area"),
         QStringLiteral("office-chart-area-stacked"),
         QStringLiteral("labplot-xy-curve-smooth"),
         QStringLiteral("sensors")},
        paintSpectrogramIcon());

    m_viewModeGroup = new QActionGroup(this);
    m_viewModeGroup->setExclusive(true);

    m_waveformViewAction = new QAction(waveIcon, tr("&Waveform"), this);
    m_waveformViewAction->setStatusTip(tr("Show bipolar intensity waveform (−1…+1)"));
    m_waveformViewAction->setCheckable(true);
    m_waveformViewAction->setChecked(true);
    m_waveformViewAction->setToolTip(tr("Waveform"));
    m_viewModeGroup->addAction(m_waveformViewAction);

    auto paintAbsIcon = []() {
        QPixmap pix(24, 24);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(40, 180, 90), 2.0));
        p.drawLine(2, 12, 22, 12);
        // L up / R down abs lobes
        p.setBrush(QColor(40, 180, 90, 160));
        p.setPen(Qt::NoPen);
        QPolygonF up;
        up << QPointF(4, 12) << QPointF(8, 4) << QPointF(12, 12) << QPointF(16, 6) << QPointF(20, 12);
        p.drawPolygon(up);
        p.setBrush(QColor(40, 160, 220, 160));
        QPolygonF down;
        down << QPointF(4, 12) << QPointF(8, 20) << QPointF(12, 12) << QPointF(16, 18) << QPointF(20, 12);
        p.drawPolygon(down);
        return QIcon(pix);
    };
    m_waveformAbsAction = new QAction(paintAbsIcon(), tr("Waveform &Abs"), this);
    m_waveformAbsAction->setStatusTip(tr("Show |amplitude| with left up and right down from centre"));
    m_waveformAbsAction->setCheckable(true);
    m_waveformAbsAction->setToolTip(tr("Abs waveform (L↑ R↓)"));
    m_viewModeGroup->addAction(m_waveformAbsAction);

    m_spectrogramAction = new QAction(specIcon, tr("S&pectrogram"), this);
    m_spectrogramAction->setStatusTip(tr("Show a frequency spectrogram instead of the amplitude waveform"));
    m_spectrogramAction->setCheckable(true);
    m_spectrogramAction->setShortcut(QKeySequence(tr("Ctrl+Shift+S")));
    m_spectrogramAction->setToolTip(tr("Spectrogram"));
    m_viewModeGroup->addAction(m_spectrogramAction);

    connect(m_viewModeGroup, &QActionGroup::triggered, this, &MainWindow::onViewModeTriggered);

    m_aboutAction = new QAction(windowIcon(), tr("&About QWavRec"), this);
    m_aboutAction->setStatusTip(tr("About QWavRec"));
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
    editMenu->addAction(m_editUndoAction);
    editMenu->addAction(m_editRedoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_cutAction);
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addAction(m_deleteSelAction);
    editMenu->addAction(m_cropAction);
    editMenu->addSeparator();
    editMenu->addAction(m_normalizeAction);

    QMenu *transportMenu = menuBar()->addMenu(tr("&Transport"));
    transportMenu->addAction(m_recordAction);
    transportMenu->addAction(m_insertRecordAction);
    transportMenu->addAction(m_playAction);
    transportMenu->addAction(m_stopAction);
    transportMenu->addSeparator();
    transportMenu->addAction(m_loopAction);
    transportMenu->addSeparator();
    transportMenu->addAction(m_undoAction);   // Previous Take
    transportMenu->addAction(m_redoAction);   // Next Take

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_historyAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_waveformViewAction);
    viewMenu->addAction(m_waveformAbsAction);
    viewMenu->addAction(m_spectrogramAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_autoScaleAction);
    viewMenu->addAction(m_stereoAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setIconSize(QSize(24, 24));
    // File
    tb->addAction(m_newAction);
    tb->addAction(m_openAction);
    tb->addAction(m_saveAction);
    tb->addSeparator();
    // Clipboard (standard: Cut / Copy / Paste)
    tb->addAction(m_cutAction);
    tb->addAction(m_copyAction);
    tb->addAction(m_pasteAction);
    tb->addSeparator();
    // Document edit history
    tb->addAction(m_editUndoAction);
    tb->addAction(m_editRedoAction);
    tb->addSeparator();
    // Display mode (exclusive checkable actions ≈ radio buttons)
    tb->addAction(m_waveformViewAction);
    tb->addAction(m_waveformAbsAction);
    tb->addAction(m_spectrogramAction);
    tb->addAction(m_stereoAction);

    // Push take navigation to the right edge (above the Takes dock)
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addAction(m_undoAction);   // Previous Take
    tb->addAction(m_redoAction);   // Next Take
    tb->addAction(m_historyAction);
}

void MainWindow::createCentralWidget()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);

    // Group by signal path: Input (device → gain → meter), then Output.
    m_inputCombo = new QComboBox;
    m_outputCombo = new QComboBox;

    m_inputVolumeSlider = new MarkedSlider(Qt::Horizontal);
    m_inputVolumeSlider->setRange(0, 300); // 0..3×
    m_inputVolumeSlider->setMarkerValue(100); // unity
    m_inputVolumeSlider->setToolTip(tr("Microphone gain — yellow mark is unity (100%). Above = boost."));
    m_micGainLabel = new QLabel(tr("100%"));
    m_micGainLabel->setMinimumWidth(48);
    m_inputMeter = new LevelMeter;
    m_inputMeter->setMinimumWidth(140);
    m_inputMeter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_outputVolumeSlider = new QSlider(Qt::Horizontal);
    m_outputVolumeSlider->setRange(0, 100);
    m_outputVolumeSlider->setToolTip(tr("Playback volume"));
    m_playbackVolumeLabel = new QLabel(tr("80%"));
    m_playbackVolumeLabel->setMinimumWidth(48);
    m_outputMeter = new LevelMeter;
    m_outputMeter->setMinimumWidth(140);
    m_outputMeter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Each meter sits beside device + gain/volume so it spans both rows
    // (and grows with the panel when the window is resized).
    auto *ioColumn = new QVBoxLayout;
    ioColumn->setSpacing(8);

    auto *inputBlock = new QHBoxLayout;
    auto *inputForm = new QFormLayout;
    inputForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    inputForm->addRow(tr("Input"), m_inputCombo);
    auto *inGainRow = new QHBoxLayout;
    inGainRow->addWidget(m_inputVolumeSlider, 1);
    inGainRow->addWidget(m_micGainLabel);
    inputForm->addRow(tr("Mic gain"), inGainRow);
    inputBlock->addLayout(inputForm, 1);
    inputBlock->addWidget(m_inputMeter, 1);
    ioColumn->addLayout(inputBlock);

    auto *outputBlock = new QHBoxLayout;
    auto *outputForm = new QFormLayout;
    outputForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    outputForm->addRow(tr("Output"), m_outputCombo);
    auto *outVolRow = new QHBoxLayout;
    outVolRow->addWidget(m_outputVolumeSlider, 1);
    outVolRow->addWidget(m_playbackVolumeLabel);
    outputForm->addRow(tr("Volume"), outVolRow);
    outputBlock->addLayout(outputForm, 1);
    outputBlock->addWidget(m_outputMeter, 1);
    ioColumn->addLayout(outputBlock);

    mainLayout->addLayout(ioColumn);

    m_waveform = new WaveformWidget;
    m_waveform->setToolTip(tr(
        "Drag near A or B to adjust edges.\n"
        "Drag elsewhere to select a new A-B region (limits play/loop).\n"
        "Click to seek. Double-click clears selection. Right-click for edit menu."));
    mainLayout->addWidget(m_waveform, 1);

    m_seekSlider = new SeekSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    mainLayout->addWidget(m_seekSlider);

    m_timeLabel = new QLabel(tr("00:00.000 / 00:00.000"));
    m_timeLabel->setAlignment(Qt::AlignCenter);
    QFont timeFont = m_timeLabel->font();
    timeFont.setPointSizeF(timeFont.pointSizeF() + 1.0);
    timeFont.setBold(true);
    m_timeLabel->setFont(timeFont);
    mainLayout->addWidget(m_timeLabel);

    // Prominent A–B readout (live while dragging); hidden when no selection
    m_selectionLabel = new QLabel;
    m_selectionLabel->setAlignment(Qt::AlignCenter);
    m_selectionLabel->setVisible(false);
    m_selectionLabel->setStyleSheet(QStringLiteral("color: #c8a020;"));
    mainLayout->addWidget(m_selectionLabel);

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

    // Mode toggles under the main transport (Insert + Loop)
    auto *toggleLayout = new QHBoxLayout;
    toggleLayout->setSpacing(8);
    auto makeToggle = [](QAction *a) {
        auto *btn = new QToolButton;
        btn->setDefaultAction(a);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setIconSize(QSize(20, 20));
        btn->setMinimumHeight(32);
        btn->setCheckable(true);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        return btn;
    };
    toggleLayout->addStretch();
    toggleLayout->addWidget(makeToggle(m_insertRecordAction));
    toggleLayout->addWidget(makeToggle(m_loopAction));
    toggleLayout->addStretch();
    mainLayout->addLayout(toggleLayout);

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
    const bool insertRec = s.value(QStringLiteral("playback/insertRecord"), false).toBool();
    const bool autoScale = s.value(QStringLiteral("view/autoScaleWaveform"), false).toBool();
    // view/mode preferred; view/spectrogram kept as legacy fallback inside block below

    m_restoringSettings = true;
    m_inputVolumeSlider->setValue(qBound(0, mic, 300));
    m_outputVolumeSlider->setValue(qBound(0, out, 100));
    m_loopAction->setChecked(loop);
    m_player->setLoop(loop);
    if (m_insertRecordAction) {
        m_insertRecordAction->setChecked(insertRec);
        m_insertRecord = insertRec;
    }
    m_autoScaleWaveform = autoScale;
    m_preferStereo = s.value(QStringLiteral("capture/stereo"), true).toBool();
    if (m_stereoAction)
        m_stereoAction->setChecked(m_preferStereo);
    m_autoScaleAction->setChecked(autoScale);
    {
        const QString vm = s.value(QStringLiteral("view/mode"), QString()).toString();
        if (vm == QLatin1String("abs"))
            m_viewMode = ViewMode::WaveformAbs;
        else if (vm == QLatin1String("spectrogram")
                 || s.value(QStringLiteral("view/spectrogram"), false).toBool())
            m_viewMode = ViewMode::Spectrogram;
        else
            m_viewMode = ViewMode::Waveform;
    }
    if (m_waveformViewAction && m_waveformAbsAction && m_spectrogramAction) {
        m_waveformViewAction->setChecked(m_viewMode == ViewMode::Waveform);
        m_waveformAbsAction->setChecked(m_viewMode == ViewMode::WaveformAbs);
        m_spectrogramAction->setChecked(m_viewMode == ViewMode::Spectrogram);
    }
    if (m_waveform) {
        if (m_viewMode == ViewMode::Spectrogram)
            m_waveform->setDisplayMode(WaveformWidget::DisplayMode::Spectrogram);
        else if (m_viewMode == ViewMode::WaveformAbs)
            m_waveform->setDisplayMode(WaveformWidget::DisplayMode::WaveformAbs);
        else
            m_waveform->setDisplayMode(WaveformWidget::DisplayMode::Waveform);
    }
    const bool showTakes = s.value(QStringLiteral("view/takesPanel"), false).toBool();
    if (m_takesDock) {
        m_takesDock->setVisible(showTakes);
        m_historyAction->setChecked(showTakes);
        if (showTakes)
            refreshTakesPanel();
    }
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
    s.setValue(QStringLiteral("playback/insertRecord"),
               m_insertRecordAction && m_insertRecordAction->isChecked());
    s.setValue(QStringLiteral("view/autoScaleWaveform"), m_autoScaleWaveform);
    s.setValue(QStringLiteral("capture/stereo"), m_preferStereo);
    {
        QString vm = QStringLiteral("waveform");
        if (m_viewMode == ViewMode::WaveformAbs)
            vm = QStringLiteral("abs");
        else if (m_viewMode == ViewMode::Spectrogram)
            vm = QStringLiteral("spectrogram");
        s.setValue(QStringLiteral("view/mode"), vm);
        s.setValue(QStringLiteral("view/spectrogram"), m_viewMode == ViewMode::Spectrogram);
    }
    s.setValue(QStringLiteral("view/takesPanel"), m_takesDock && m_takesDock->isVisible());
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
    if (m_player) {
        m_player->stop();
        m_player->loadPcm(QByteArray(), QAudioFormat());
        m_player->clearPlayRange();
    }
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
    m_timeLabel->setText(tr("00:00.000 / 00:00.000"));
    m_waveform->clear();
    m_liveRecordPeaks.clear();
    m_rawPeaks.clear();
    m_editUndo.clear();
    m_editRedo.clear();
    // Keep m_clipPcm / m_clipFormat so Paste works across New / take switches
    updateWindowTitle();
    setAppState(AppState::Ready);
    updateEditActions();
    startMonitoring();
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

void MainWindow::applyPeaksToWaveform(const QVector<float> &left, const QVector<float> &right)
{
    if (left.isEmpty()) {
        m_waveform->setPeaks({});
        return;
    }
    float mx = 0.f;
    for (float v : left)
        mx = qMax(mx, v);
    for (float v : right)
        mx = qMax(mx, v);

    auto scale = [mx](QVector<float> v) {
        if (mx > 1e-6f) {
            for (float &x : v)
                x /= mx;
        }
        return v;
    };

    if (!right.isEmpty() && right.size() == left.size()) {
        if (m_autoScaleWaveform)
            m_waveform->setChannelPeaks(scale(left), scale(right));
        else
            m_waveform->setChannelPeaks(left, right);
    } else {
        m_waveform->setPeaks(m_autoScaleWaveform ? scale(left) : left);
    }
}

void MainWindow::setWaveformFromPcm(const QByteArray &pcm, const QAudioFormat &fmt)
{
    if (m_outputMeter)
        m_outputMeter->setStereo(fmt.channelCount() >= 2);
    const int bins = peakBinCount();
    const WavFile::ChannelPeaks ch = WavFile::channelPeaks(pcm, fmt, bins);
    m_rawPeaks = WavFile::peaks(pcm, fmt, bins);
    if (m_rawPeaks.isEmpty()) {
        m_waveform->clear();
        return;
    }
    applyPeaksToWaveform(ch.left, ch.right);

    // Oscilloscope-style density image (sub-pixel activity). Scale matches auto-scale peaks.
    float densScale = 1.f;
    if (m_autoScaleWaveform) {
        float mx = 0.f;
        for (float v : m_rawPeaks)
            mx = qMax(mx, v);
        if (mx > 1e-6f)
            densScale = 1.f / mx;
    }
    const int timeBins = qBound(64, bins, 4096);
    // Amplitude bins ≈ widget height for 1:1 vertical detail when painted.
    const int ampBins = qBound(64, m_waveform ? m_waveform->height() : 128, 512);
    m_waveform->setWaveformDensity(WavFile::waveformDensity(pcm, fmt, timeBins, ampBins, densScale));
    m_waveform->setWaveformDensityAbs(WavFile::waveformDensityAbs(pcm, fmt, timeBins, ampBins, densScale));

    if (m_viewMode == ViewMode::Spectrogram && !pcm.isEmpty()) {
        // Match horizontal resolution to the drawable width for detail when maximized.
        const int specBins = qBound(64, peakBinCount(), 2048);
        m_waveform->setSpectrogram(WavFile::spectrogram(pcm, fmt, specBins, 256));
    } else {
        m_waveform->setSpectrogram(QImage());
    }
    if (m_viewMode == ViewMode::Spectrogram)
        m_waveform->setDisplayMode(WaveformWidget::DisplayMode::Spectrogram);
    else if (m_viewMode == ViewMode::WaveformAbs)
        m_waveform->setDisplayMode(WaveformWidget::DisplayMode::WaveformAbs);
    else
        m_waveform->setDisplayMode(WaveformWidget::DisplayMode::Waveform);
}

int MainWindow::peakBinCount() const
{
    // About one peak column per pixel; clamp so tiny/huge windows stay sane.
    const int w = m_waveform ? m_waveform->contentWidth() : 400;
    return qBound(64, w, 4096);
}

int MainWindow::currentSourceChannelCount() const
{
    if (!m_deviceWatcher)
        return 1;
    const QString src = currentSourceName();
    if (src.isEmpty())
        return 1;
    const PulseDeviceLists lists = m_deviceWatcher->snapshot();
    for (const PulseDevice &d : lists.sources) {
        if (d.name == src)
            return qMax(1, d.channelCount);
    }
    return 1;
}

int MainWindow::captureChannelCount() const
{
    if (!m_preferStereo)
        return 1;
    // Cap at stereo for the UI/meter model; >2 channels still record as stereo
    // only when we request 2 — Pulse remixes. Prefer matching device when ≥2.
    return currentSourceChannelCount() >= 2 ? 2 : 1;
}

QAudioFormat MainWindow::captureFormat() const
{
    QAudioFormat fmt;
    fmt.setSampleFormat(QAudioFormat::Int16);
    fmt.setSampleRate(48000);
    fmt.setChannelCount(captureChannelCount());
    return fmt;
}

void MainWindow::onStereoToggled(bool on)
{
    m_preferStereo = on;
    if (m_inputMeter)
        m_inputMeter->setStereo(captureChannelCount() >= 2);
    // Restart monitoring with the new channel layout when idle.
    if (m_state != AppState::Recording
        && (m_state == AppState::Ready || m_state == AppState::Error
            || m_state == AppState::Playing || m_state == AppState::Paused)) {
        // Force restart even if same device name
        m_monitorSourceName.clear();
        startMonitoring();
    }
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::rebuildPeaksFromDocument()
{
    if (m_state == AppState::Recording) {
        // Live path recomputes on the next meter tick with current width.
        return;
    }
    if (m_player && !m_player->pcm().isEmpty())
        setWaveformFromPcm(m_player->pcm(), m_player->format());
}

void MainWindow::onWaveformWidthChanged(int width)
{
    Q_UNUSED(width);
    // Debounce: maximize/tile animations fire many resizes; full-PCM peak
    // rebuild is O(n) and should not run on every intermediate width.
    if (!m_waveformResizeTimer) {
        m_waveformResizeTimer = new QTimer(this);
        m_waveformResizeTimer->setSingleShot(true);
        m_waveformResizeTimer->setInterval(50);
        connect(m_waveformResizeTimer, &QTimer::timeout, this, &MainWindow::rebuildPeaksFromDocument);
    }
    m_waveformResizeTimer->start();
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
    m_editUndo.clear();
    m_editRedo.clear();
    updateEditActions();
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
        finishRecordingStop();
        return;
    }

    // Keep action state as source of truth
    m_insertRecord = m_insertRecordAction && m_insertRecordAction->isChecked();
    m_insertBasePcm.clear();
    m_insertBaseFormat = QAudioFormat();
    m_insertBasePeaks.clear();
    m_insertBaseDurationMs = 0;
    m_insertAtMs = 0;
    m_captureTempPath.clear();

    // Insert with an empty document is pointless — behave as a normal record.
    if (m_insertRecord && (!m_player || m_player->pcm().isEmpty()))
        m_insertRecord = false;

    if (m_insertRecord) {
        // Snapshot document before anything can change it
        m_insertBasePcm = m_player->pcm();
        m_insertBaseFormat = m_player->format();
        m_insertBaseDurationMs = m_player->duration();
        m_insertBasePeaks = m_rawPeaks;
        if (m_insertBasePeaks.isEmpty())
            m_insertBasePeaks = WavFile::peaks(m_insertBasePcm, m_insertBaseFormat, peakBinCount());
        m_insertAtMs = m_player->position();
        if (m_seekSlider && m_seekSlider->value() > m_insertAtMs)
            m_insertAtMs = m_seekSlider->value();
        m_insertAtMs = qBound(qint64(0), m_insertAtMs, m_player->duration());
        if (m_state == AppState::Playing || m_state == AppState::Paused)
            m_player->stop();
    } else {
        if (m_state == AppState::Playing || m_state == AppState::Paused)
            m_player->stop();
        if (!m_tempPath.isEmpty() && m_isTemporary)
            QFile::remove(m_tempPath);
        m_savedPath.clear();
    }

    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("qwavrec-XXXXXX.wav")));
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        QMessageBox::critical(this, tr("Error"), tr("Could not create temporary file."));
        m_recordAction->setChecked(false);
        return;
    }
    m_captureTempPath = tmp.fileName();
    tmp.close();

    if (!m_insertRecord) {
        m_tempPath = m_captureTempPath;
        m_isTemporary = true;
    }

    // Keep the same capture stream — just enable PCM queue
    if (!m_monitoring || !m_capture->isRunning()) {
        stopMonitoring();
        m_capture->setGain(m_inputVolumeSlider->value() / 100.0);
        const QString src = currentSourceName();
        if (!m_capture->start(src, 48000, captureChannelCount())) {
            QMessageBox::critical(this, tr("Error"), tr("Could not open PulseAudio source for recording."));
            m_recordAction->setChecked(false);
            return;
        }
        m_monitorSourceName = src;
        m_monitoring = true;
    }
    m_capture->setGain(m_inputVolumeSlider->value() / 100.0);

    QAudioFormat fmt = captureFormat();
    // Prefer the live capture format if the stream is already open
    if (m_capture && m_capture->isRunning() && m_capture->format().channelCount() > 0)
        fmt = m_capture->format();
    m_recordChannelCount = qMax(1, fmt.channelCount());
    fmt.setChannelCount(m_recordChannelCount);
    if (!m_wavWriter.open(m_captureTempPath, fmt)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open WAV file for writing."));
        m_recordAction->setChecked(false);
        return;
    }

    m_liveRecordPeaks.clear();
    m_recordPcm.clear();
    if (!m_insertRecord) {
        m_waveform->clear();
    } else {
        // Keep peaks for insert preview; drop stale density/spectrogram so live falls back to peak paint.
        m_waveform->setWaveformDensity(QImage());
        m_waveform->setWaveformDensityAbs(QImage());
        m_waveform->setSpectrogram(QImage());
    }
    m_recordTimer.restart();
    markModified();
    setAppState(AppState::Recording);
    m_recordAction->setChecked(true);
    m_capture->setRecording(true);
    if (m_insertRecord)
        statusBar()->showMessage(tr("Insert recording at %1…").arg(formatTime(m_insertAtMs)), 0);
    else if (m_history.takes().size() > 0)
        statusBar()->showMessage(
            tr("Recording… (previous takes remain in cache, %1 total)")
                .arg(m_history.takes().size()), 0);
}

void MainWindow::finishRecordingStop()
{
    m_capture->setRecording(false);

    for (int i = 0; i < 3; ++i) {
        const QByteArray rest = m_capture->takeRecordedAudio();
        if (rest.isEmpty())
            break;
        m_recordPcm.append(rest);
        if (m_wavWriter.isOpen())
            m_wavWriter.write(rest.constData(), rest.size());
    }
    m_wavWriter.close();
    m_recordAction->setChecked(false);

    const bool wantInsert = m_insertRecord && !m_insertBasePcm.isEmpty();

    // Final waveform from full PCM (same path as Open / load)
    if (!wantInsert && !m_recordPcm.isEmpty()) {
        QAudioFormat liveFmt;
        liveFmt.setSampleFormat(QAudioFormat::Int16);
        liveFmt.setSampleRate(48000);
        liveFmt.setChannelCount(qMax(1, m_recordChannelCount));
        setWaveformFromPcm(m_recordPcm, liveFmt);
    }

    setAppState(AppState::Ready);
    // Always re-arm the input meter. Leaving the stream "as is" is fragile:
    // isRunning() can already be false (read error, prior stopMonitoring from
    // Play, session race), and we used to only copy that flag — so the meter
    // stayed dead after a take. startMonitoring() is a no-op if the same
    // source is already open.
    startMonitoring();

    if (m_recordPcm.isEmpty()) {
        if (!m_captureTempPath.isEmpty())
            QFile::remove(m_captureTempPath);
        m_captureTempPath.clear();
        m_insertBasePcm.clear();
        m_recordPcm.clear();
        statusBar()->showMessage(tr("Recording produced no audio data."), 3000);
        return;
    }

    QAudioFormat capFmt;
    capFmt.setSampleFormat(QAudioFormat::Int16);
    capFmt.setSampleRate(48000);
    capFmt.setChannelCount(qMax(1, m_recordChannelCount));

    if (wantInsert) {
        const QAudioFormat baseFmt = m_insertBaseFormat;
        const bool fmtOk =
            baseFmt.sampleFormat() == capFmt.sampleFormat()
            && baseFmt.sampleRate() == capFmt.sampleRate()
            && baseFmt.channelCount() == capFmt.channelCount();
        if (!fmtOk) {
            QMessageBox::warning(this, tr("Insert"),
                tr("Cannot insert: the document format does not match the capture format "
                   "(need 48 kHz 16-bit, matching channel count).\n"
                   "The new audio was discarded; the document is unchanged."));
            if (!m_captureTempPath.isEmpty())
                QFile::remove(m_captureTempPath);
            m_captureTempPath.clear();
            m_insertBasePcm.clear();
            m_recordPcm.clear();
            return;
        }

        pushEditUndo(tr("Insert recording"));
        const int bpf = capFmt.bytesPerFrame();
        const int frames = m_insertBasePcm.size() / bpf;
        int at = 0;
        if (capFmt.sampleRate() > 0)
            at = int(capFmt.framesForDuration(m_insertAtMs * 1000));
        at = qBound(0, at, frames);

        QByteArray out;
        out.reserve(m_insertBasePcm.size() + m_recordPcm.size());
        out.append(m_insertBasePcm.constData(), at * bpf);
        out.append(m_recordPcm);
        out.append(m_insertBasePcm.constData() + at * bpf, (frames - at) * bpf);

        if (!m_captureTempPath.isEmpty())
            QFile::remove(m_captureTempPath);
        m_captureTempPath.clear();
        m_insertBasePcm.clear();
        m_insertBasePeaks.clear();
        m_recordPcm.clear();

        applyDocumentPcm(out, capFmt);
        statusBar()->showMessage(tr("Inserted recording at %1").arg(formatTime(m_insertAtMs)), 4000);
        return;
    }

    // New take path
    if (!m_tempPath.isEmpty() && m_tempPath != m_captureTempPath && m_isTemporary)
        QFile::remove(m_tempPath);
    if (!m_captureTempPath.isEmpty())
        m_tempPath = m_captureTempPath;
    m_isTemporary = true;

    m_player->loadPcm(m_recordPcm, capFmt);
    m_player->clearPlayRange();
    m_waveform->clearSelection();
    m_duration = m_player->duration();
    m_seekSlider->setRange(0, int(m_duration));
    updateTimeLabel();
    updateWindowTitle();
    m_editUndo.clear();
    m_editRedo.clear();
    updateEditActions();

    const QString tempPath = m_tempPath;
    const bool isTemp = m_isTemporary;
    QTimer::singleShot(0, this, [this, tempPath, isTemp]() {
        if (tempPath.isEmpty() || !QFileInfo::exists(tempPath))
            return;
        const QString archived = m_history.archiveTake(tempPath);
        if (!archived.isEmpty()) {
            if (isTemp)
                QFile::remove(tempPath);
            m_tempPath = archived;
            m_isTemporary = false;
            m_modified = true;
            updateWindowTitle();
            statusBar()->showMessage(
                tr("Take saved to cache (%1)").arg(m_history.takes().size()), 4000);
            refreshTakesPanel();
        } else {
            statusBar()->showMessage(tr("Could not archive take to cache"), 4000);
        }
        m_recordPcm.clear();
        m_captureTempPath.clear();
    });
}

void MainWindow::onPlay()
{
    if (m_state == AppState::Recording) {
        m_playAction->setChecked(false);
        return;
    }
    if (m_state == AppState::Playing) {
        // Toggle → pause. Button is unchecked in onPlayerStateChanged(Paused).
        m_player->pause();
        return;
    }
    if (m_state == AppState::Paused) {
        m_player->setSinkName(currentSinkName());
        m_player->play();
        return;
    }

    // Ready / Error: start playback of the current document.
    const QString path = documentPathForPlayback();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        // In-memory PCM from a just-finished take may still be in the player.
        if (m_player->duration() <= 0) {
            QMessageBox::information(this, tr("QWavRec"),
                tr("Nothing to play. Record something or open a WAV file first."));
            m_playAction->setChecked(false);
            return;
        }
    } else {
        const WavFile::Info info = WavFile::load(path);
        if (!info.ok) {
            QMessageBox::warning(this, tr("Play"), info.error);
            m_playAction->setChecked(false);
            return;
        }
        m_player->loadPcm(info.pcm, info.format);
        m_player->clearPlayRange();
        applySelectionToPlayer();
    }
    // Keep capture running so the input meter stays live during playback.
    // Pulse handles concurrent record+playback streams; stopping here only
    // blanked the meter until play ended (see onPlayerStateChanged → startMonitoring).
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
        // Leave capture running for the input meter (stop is non-blocking
        // but restarting monitoring is unnecessary work and drops the meter).
        m_seekSlider->setValue(0);
        m_waveform->setPlaybackPosition(0.0);
        updateTimeLabel();
        m_playAction->setChecked(false);
    }
}

void MainWindow::onNormalize()
{
    if (m_state == AppState::Recording || m_state == AppState::Playing)
        return;
    if (!hasDocument() || !m_player || m_player->pcm().isEmpty()) {
        QMessageBox::information(this, tr("QWavRec"), tr("Nothing to normalize."));
        return;
    }
    QByteArray pcm = m_player->pcm();
    const QAudioFormat fmt = m_player->format();
    if (fmt.sampleFormat() != QAudioFormat::Int16) {
        QMessageBox::warning(this, tr("Normalize"),
            tr("Could not normalize this file.\n"
               "Only 16-bit PCM WAV is supported."));
        return;
    }
    pushEditUndo(tr("Normalize"));
    if (!WavFile::peakNormalizeInt16(pcm)) {
        m_editUndo.removeLast();
        return;
    }
    applyDocumentPcm(pcm, fmt);
    statusBar()->showMessage(tr("Normalized to peak"), 3000);
}

bool MainWindow::normalizeCurrentFile()
{
    // Kept for any legacy callers; prefer onNormalize() in-memory path.
    onNormalize();
    return hasDocument();
}

void MainWindow::pushEditUndo(const QString &label)
{
    if (!m_player)
        return;
    EditSnap snap;
    snap.pcm = m_player->pcm();
    snap.format = m_player->format();
    snap.label = label;
    m_editUndo.append(snap);
    while (m_editUndo.size() > kMaxEditUndo)
        m_editUndo.removeFirst();
    m_editRedo.clear();
    updateEditActions();
}

void MainWindow::applyDocumentPcm(const QByteArray &pcm, const QAudioFormat &fmt)
{
    if (m_state == AppState::Playing || m_state == AppState::Paused)
        m_player->stop();
    m_player->loadPcm(pcm, fmt);
    m_player->clearPlayRange();
    setWaveformFromPcm(pcm, fmt);
    m_waveform->clearSelection();
    m_duration = m_player->duration();
    m_seekSlider->setRange(0, int(m_duration));
    m_seekSlider->setValue(0);
    m_waveform->setPlaybackPosition(0.0);
    updateTimeLabel();
    writeDocumentPcm(pcm, fmt);
    markModified();
    updateWindowTitle();
    updateEditActions();
    applySelectionToPlayer();
}

bool MainWindow::writeDocumentPcm(const QByteArray &pcm, const QAudioFormat &fmt)
{
    // Copy-on-write if current path is a cache take (do not mutate history files).
    QString path = m_tempPath;
    const bool inCache = !path.isEmpty() && path.startsWith(m_history.cacheDir());
    if (path.isEmpty() || inCache || (!m_isTemporary && !m_savedPath.isEmpty() && path == m_savedPath)) {
        QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("qwavrec-edit-XXXXXX.wav")));
        tmp.setAutoRemove(false);
        if (!tmp.open())
            return false;
        path = tmp.fileName();
        tmp.close();
        m_tempPath = path;
        m_isTemporary = true;
        // keep m_savedPath if user had an export target
    }
    WavWriter writer;
    if (!writer.open(path, fmt))
        return false;
    writer.write(pcm.constData(), pcm.size());
    writer.close();
    return true;
}

QByteArray MainWindow::selectionPcm(const QByteArray &pcm, const QAudioFormat &fmt,
                                    qreal a, qreal b) const
{
    const int bpf = fmt.bytesPerFrame();
    if (bpf <= 0 || pcm.isEmpty() || b <= a)
        return {};
    const int frames = pcm.size() / bpf;
    int fa = qBound(0, int(a * frames), frames);
    int fb = qBound(0, int(b * frames), frames);
    if (fb < fa)
        qSwap(fa, fb);
    return pcm.mid(fa * bpf, (fb - fa) * bpf);
}

void MainWindow::updateEditActions()
{
    const bool idle = (m_state == AppState::Ready || m_state == AppState::Paused
                       || m_state == AppState::Error);
    const bool playing = (m_state == AppState::Playing);
    const bool hasDoc = m_player && !m_player->pcm().isEmpty();
    const bool hasSel = m_waveform && m_waveform->hasSelection();
    const bool canEdit = idle && hasDoc;
    if (m_cutAction)
        m_cutAction->setEnabled(canEdit && hasSel);
    // Copy is non-destructive — allowed while playing.
    if (m_copyAction)
        m_copyAction->setEnabled((canEdit || (playing && hasDoc)) && hasSel);
    // Paste works into an empty document (File→New) as well as into an existing take
    if (m_pasteAction)
        m_pasteAction->setEnabled(idle && !m_clipPcm.isEmpty());
    if (m_deleteSelAction)
        m_deleteSelAction->setEnabled(canEdit && hasSel);
    if (m_cropAction)
        m_cropAction->setEnabled(canEdit && hasSel);
    if (m_editUndoAction)
        m_editUndoAction->setEnabled(idle && !m_editUndo.isEmpty());
    if (m_editRedoAction)
        m_editRedoAction->setEnabled(idle && !m_editRedo.isEmpty());
    if (m_normalizeAction)
        m_normalizeAction->setEnabled(canEdit);
    if (m_insertRecordAction)
        m_insertRecordAction->setEnabled(m_state != AppState::Recording);
}

void MainWindow::onCopy()
{
    if (!m_player || !m_waveform || !m_waveform->hasSelection())
        return;
    m_clipPcm = selectionPcm(m_player->pcm(), m_player->format(),
                             m_waveform->selectionStart(), m_waveform->selectionEnd());
    m_clipFormat = m_player->format();
    updateEditActions();
    statusBar()->showMessage(tr("Copied selection"), 2000);
}

void MainWindow::onCut()
{
    if (!m_player || !m_waveform || !m_waveform->hasSelection())
        return;
    onCopy();
    onDeleteSelection();
}

void MainWindow::onDeleteSelection()
{
    if (!m_player || !m_waveform || !m_waveform->hasSelection())
        return;
    const QAudioFormat fmt = m_player->format();
    const int bpf = fmt.bytesPerFrame();
    if (bpf <= 0)
        return;
    QByteArray pcm = m_player->pcm();
    const int frames = pcm.size() / bpf;
    const int fa = qBound(0, int(m_waveform->selectionStart() * frames), frames);
    const int fb = qBound(0, int(m_waveform->selectionEnd() * frames), frames);
    pushEditUndo(tr("Delete selection"));
    QByteArray out;
    out.reserve(pcm.size() - (fb - fa) * bpf);
    out.append(pcm.constData(), fa * bpf);
    out.append(pcm.constData() + fb * bpf, (frames - fb) * bpf);
    applyDocumentPcm(out, fmt);
    statusBar()->showMessage(tr("Deleted selection"), 2000);
}

void MainWindow::onCropToSelection()
{
    if (!m_player || !m_waveform || !m_waveform->hasSelection())
        return;
    const QAudioFormat fmt = m_player->format();
    QByteArray sel = selectionPcm(m_player->pcm(), fmt,
                                  m_waveform->selectionStart(), m_waveform->selectionEnd());
    if (sel.isEmpty())
        return;
    pushEditUndo(tr("Crop to selection"));
    applyDocumentPcm(sel, fmt);
    statusBar()->showMessage(tr("Cropped to selection"), 2000);
}

void MainWindow::onPaste()
{
    if (!m_player || m_clipPcm.isEmpty())
        return;
    if (m_state == AppState::Recording || m_state == AppState::Playing)
        return;

    // Empty document (e.g. File→New): paste creates the document from the clipboard
    if (m_player->pcm().isEmpty()) {
        pushEditUndo(tr("Paste"));
        applyDocumentPcm(m_clipPcm, m_clipFormat);
        statusBar()->showMessage(tr("Pasted into new document"), 2000);
        return;
    }

    const QAudioFormat fmt = m_player->format();
    if (fmt.sampleFormat() != m_clipFormat.sampleFormat()
        || fmt.sampleRate() != m_clipFormat.sampleRate()
        || fmt.channelCount() != m_clipFormat.channelCount()) {
        QMessageBox::warning(this, tr("Paste"),
            tr("Clipboard audio format does not match the current document."));
        return;
    }
    const int bpf = fmt.bytesPerFrame();
    if (bpf <= 0)
        return;
    QByteArray pcm = m_player->pcm();
    const int frames = pcm.size() / bpf;
    if (m_waveform->hasSelection()) {
        const int fa = qBound(0, int(m_waveform->selectionStart() * frames), frames);
        const int fb = qBound(0, int(m_waveform->selectionEnd() * frames), frames);
        pushEditUndo(tr("Paste (replace selection)"));
        QByteArray out;
        out.append(pcm.constData(), fa * bpf);
        out.append(m_clipPcm);
        out.append(pcm.constData() + fb * bpf, (frames - fb) * bpf);
        applyDocumentPcm(out, fmt);
    } else {
        const qint64 posMs = m_player->position();
        int insertFrame = int(fmt.framesForDuration(posMs * 1000));
        insertFrame = qBound(0, insertFrame, frames);
        pushEditUndo(tr("Paste"));
        QByteArray out;
        out.append(pcm.constData(), insertFrame * bpf);
        out.append(m_clipPcm);
        out.append(pcm.constData() + insertFrame * bpf, (frames - insertFrame) * bpf);
        applyDocumentPcm(out, fmt);
    }
    statusBar()->showMessage(tr("Pasted"), 2000);
}

void MainWindow::onEditUndo()
{
    if (m_editUndo.isEmpty() || !m_player)
        return;
    if (m_state == AppState::Recording || m_state == AppState::Playing)
        return;
    EditSnap cur;
    cur.pcm = m_player->pcm();
    cur.format = m_player->format();
    cur.label = tr("Redo point");
    m_editRedo.append(cur);
    const EditSnap snap = m_editUndo.takeLast();
    // apply without pushing undo
    if (m_state == AppState::Paused)
        m_player->stop();
    m_player->loadPcm(snap.pcm, snap.format);
    m_player->clearPlayRange();
    setWaveformFromPcm(snap.pcm, snap.format);
    m_waveform->clearSelection();
    m_duration = m_player->duration();
    m_seekSlider->setRange(0, int(m_duration));
    writeDocumentPcm(snap.pcm, snap.format);
    markModified();
    updateWindowTitle();
    updateEditActions();
    statusBar()->showMessage(tr("Undo: %1").arg(snap.label), 2500);
}

void MainWindow::onEditRedo()
{
    if (m_editRedo.isEmpty() || !m_player)
        return;
    if (m_state == AppState::Recording || m_state == AppState::Playing)
        return;
    EditSnap cur;
    cur.pcm = m_player->pcm();
    cur.format = m_player->format();
    cur.label = tr("Undo point");
    m_editUndo.append(cur);
    const EditSnap snap = m_editRedo.takeLast();
    if (m_state == AppState::Paused)
        m_player->stop();
    m_player->loadPcm(snap.pcm, snap.format);
    m_player->clearPlayRange();
    setWaveformFromPcm(snap.pcm, snap.format);
    m_waveform->clearSelection();
    m_duration = m_player->duration();
    m_seekSlider->setRange(0, int(m_duration));
    writeDocumentPcm(snap.pcm, snap.format);
    markModified();
    updateWindowTitle();
    updateEditActions();
    statusBar()->showMessage(tr("Redo: %1").arg(snap.label), 2500);
}

void MainWindow::onWaveformContextMenu(const QPoint &globalPos)
{
    updateEditActions();
    QMenu menu(this);
    menu.addAction(m_editUndoAction);
    menu.addAction(m_editRedoAction);
    menu.addSeparator();
    menu.addAction(m_cutAction);
    menu.addAction(m_copyAction);
    menu.addAction(m_pasteAction);
    menu.addAction(m_deleteSelAction);
    menu.addAction(m_cropAction);
    menu.addSeparator();
    menu.addAction(m_normalizeAction);
    menu.exec(globalPos);
}

void MainWindow::onInsertRecordToggled(bool on)
{
    m_insertRecord = on;
    if (!m_restoringSettings)
        statusBar()->showMessage(
            on ? tr("Insert record mode: next recording splices at the playhead")
               : tr("Normal record mode: next recording starts a new take"),
            3500);
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
    // Rebuild density with the new scale factor when document PCM is available.
    if (m_player && !m_player->pcm().isEmpty() && m_state != AppState::Recording)
        setWaveformFromPcm(m_player->pcm(), m_player->format());
    else if (!m_rawPeaks.isEmpty())
        m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::onViewModeTriggered(QAction *action)
{
    if (!action)
        return;
    ViewMode mode = ViewMode::Waveform;
    if (action == m_waveformAbsAction)
        mode = ViewMode::WaveformAbs;
    else if (action == m_spectrogramAction)
        mode = ViewMode::Spectrogram;
    if (m_viewMode == mode)
        return;
    m_viewMode = mode;
    applyDisplayMode();
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::applyDisplayMode()
{
    if (!m_waveform)
        return;
    if (m_viewMode == ViewMode::Spectrogram)
        m_waveform->setDisplayMode(WaveformWidget::DisplayMode::Spectrogram);
    else if (m_viewMode == ViewMode::WaveformAbs)
        m_waveform->setDisplayMode(WaveformWidget::DisplayMode::WaveformAbs);
    else
        m_waveform->setDisplayMode(WaveformWidget::DisplayMode::Waveform);

    // Rebuild analysis images from the current document when available.
    if (m_player && !m_player->pcm().isEmpty() && m_state != AppState::Recording) {
        setWaveformFromPcm(m_player->pcm(), m_player->format());
        return;
    }
    if (m_state == AppState::Recording && !m_recordPcm.isEmpty()
        && m_viewMode == ViewMode::Spectrogram) {
        QAudioFormat liveFmt;
        liveFmt.setSampleFormat(QAudioFormat::Int16);
        liveFmt.setSampleRate(48000);
        liveFmt.setChannelCount(qMax(1, m_recordChannelCount));
        m_waveform->setSpectrogram(WavFile::spectrogram(
            m_recordPcm, liveFmt, qBound(64, peakBinCount(), 2048), 256));
    }
}



void MainWindow::activateHistoryTake(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;

    const bool resumePlay = (m_state == AppState::Playing);
    // loadPcm() stops the stream; suppress Stopped→Ready/monitoring until we decide.
    m_resumePlayAfterTakeLoad = resumePlay;

    m_tempPath = path;
    m_isTemporary = false;
    m_savedPath.clear();
    m_modified = true;
    loadDocumentForPlayback(path);
    updateWindowTitle();
    statusBar()->showMessage(
        tr("Loaded %1").arg(QFileInfo(path).fileName()), 3000);
    refreshTakesPanel();

    if (resumePlay && m_player && m_player->duration() > 0) {
        m_resumePlayAfterTakeLoad = false;
        // Same path as onPlay: leave capture up so the input meter keeps moving.
        m_player->setSinkName(currentSinkName());
        m_player->setPosition(0);
        m_player->play();
        return;
    }

    m_resumePlayAfterTakeLoad = false;
    setAppState(AppState::Ready);
    startMonitoring();
}

void MainWindow::onUndo()
{
    if (m_state == AppState::Recording)
        return;
    const QString path = m_history.previous();
    if (path.isEmpty())
        return;
    activateHistoryTake(path);
}

void MainWindow::onRedo()
{
    if (m_state == AppState::Recording)
        return;
    const QString path = m_history.next();
    if (path.isEmpty())
        return;
    activateHistoryTake(path);
}

void MainWindow::onHistory(bool show)
{
    if (!m_takesDock)
        return;
    m_takesDock->setVisible(show);
    if (show)
        refreshTakesPanel();
}

void MainWindow::refreshTakesPanel()
{
    if (!m_takesPanel)
        return;
    m_history.reload();
    m_takesPanel->setCacheDir(m_history.cacheDir());
    m_takesPanel->setTakes(m_history.takes(), m_history.currentIndex());
}

void MainWindow::loadTakeAtIndex(int index)
{
    if (m_state == AppState::Recording)
        return;
    if (!m_history.selectIndex(index))
        return;
    activateHistoryTake(m_history.currentPath());
}

void MainWindow::onTakesLoadRequested(int index)
{
    if (index == m_history.currentIndex() && hasDocument()
        && documentPathForPlayback() == m_history.currentPath())
        return;
    loadTakeAtIndex(index);
}

void MainWindow::onTakesDeleteRequested(const QList<int> &indices)
{
    if (m_state == AppState::Recording)
        return;
    if (indices.isEmpty())
        return;

    QList<int> unique;
    unique.reserve(indices.size());
    for (int index : indices) {
        if (index >= 0 && index < m_history.takes().size() && !unique.contains(index))
            unique.append(index);
    }
    if (unique.isEmpty())
        return;
    std::sort(unique.begin(), unique.end());

    QString prompt;
    if (unique.size() == 1) {
        const QString name = QFileInfo(m_history.takes().at(unique.first())).fileName();
        prompt = tr("Permanently delete “%1” from the cache?").arg(name);
    } else {
        prompt = tr("Permanently delete %1 selected takes from the cache?").arg(unique.size());
    }
    // GNOME HIG: name the action (Delete), offer Cancel; avoid Yes/No.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(unique.size() == 1 ? tr("Delete take") : tr("Delete takes"));
    box.setText(prompt);
    QAbstractButton *deleteBtn = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != deleteBtn)
        return;

    if (m_state == AppState::Playing || m_state == AppState::Paused) {
        if (m_player)
            m_player->stop();
    }

    const int current = m_history.currentIndex();
    const bool wasCurrent = unique.contains(current);

    // Remove highest index first so earlier indices stay valid.
    for (int i = unique.size() - 1; i >= 0; --i) {
        if (!m_history.removeAt(unique.at(i)))
            return;
    }

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
            setAppState(AppState::Ready);
        }
    }
    updateWindowTitle();
    refreshTakesPanel();
    statusBar()->showMessage(
        unique.size() == 1 ? tr("Deleted 1 take from cache")
                           : tr("Deleted %1 takes from cache").arg(unique.size()),
        3000);
}

void MainWindow::onAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("About QWavRec"));
    box.setTextFormat(Qt::RichText);
    box.setText(
        tr("<h3>QWavRec</h3>"
           "<p>Simple WAV recorder/player using <b>PulseAudio</b> "
           "(sources including monitors, and sinks).</p>"
           "<p>Takes are archived under <code>~/.cache/qwavrec</code>.</p>"
           "<p>Version %1 · GPL-3.0-or-later</p>"
           "<p>Project home:<br>"
           "<a href=\"https://github.com/Grumbel/qwavrec\">"
           "https://github.com/Grumbel/qwavrec</a></p>")
            .arg(QApplication::applicationVersion()));
    const QIcon icon = windowIcon();
    if (!icon.isNull())
        box.setIconPixmap(icon.pixmap(64, 64));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::onPulseDevicesChanged()
{
    if (m_deviceDebounceTimer)
        m_deviceDebounceTimer->start();
}

void MainWindow::refreshDevices()
{
    // Do not rebuild while the user is interacting with a popup list.
    if ((m_inputCombo && m_inputCombo->view() && m_inputCombo->view()->isVisible())
        || (m_outputCombo && m_outputCombo->view() && m_outputCombo->view()->isVisible()))
        return;

    QString curIn = m_pendingInputName;
    QString curOut = m_pendingOutputName;
    if (curIn.isEmpty() && m_inputCombo->currentIndex() >= 0)
        curIn = m_inputCombo->currentData().toString();
    if (curOut.isEmpty() && m_outputCombo->currentIndex() >= 0)
        curOut = m_outputCombo->currentData().toString();

    // Prefer the watcher cache (same long-lived context as hotplug). Fall back
    // to a one-shot query only before the first successful enumeration.
    PulseDeviceLists lists;
    if (m_deviceWatcher) {
        lists = m_deviceWatcher->snapshot();
        if (!lists.ok)
            lists = PulseDevices::query();
    } else {
        lists = PulseDevices::query();
    }
    if (!lists.ok)
        return;

    // Fast path: if names and count match the current combos, skip the rebuild.
    auto namesMatch = [](QComboBox *box, const QVector<PulseDevice> &devs) {
        if (!box || box->count() != devs.size())
            return false;
        for (int i = 0; i < devs.size(); ++i) {
            if (box->itemData(i).toString() != devs[i].name)
                return false;
        }
        return true;
    };
    const bool inSame = namesMatch(m_inputCombo, lists.sources);
    const bool outSame = namesMatch(m_outputCombo, lists.sinks);
    if (inSame && outSame && m_pendingInputName.isEmpty() && m_pendingOutputName.isEmpty())
        return;

    m_inputCombo->blockSignals(true);
    m_outputCombo->blockSignals(true);

    if (!inSame || !m_pendingInputName.isEmpty()) {
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
    }

    if (!outSame || !m_pendingOutputName.isEmpty()) {
        m_outputCombo->clear();
        int selOut = 0, i = 0;
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
    }

    m_inputCombo->blockSignals(false);
    m_outputCombo->blockSignals(false);
    m_pendingInputName.clear();
    m_pendingOutputName.clear();

    // Apply selection (restarts capture / updates sink name when they actually changed).
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
    updatePlaybackVolumeLabel();
    if (!m_restoringSettings)
        saveSettings();
}

void MainWindow::updatePlaybackVolumeLabel()
{
    if (!m_playbackVolumeLabel || !m_outputVolumeSlider)
        return;
    m_playbackVolumeLabel->setText(tr("%1%").arg(m_outputVolumeSlider->value()));
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
    const QString src = currentSourceName();
    const int ch = captureChannelCount();
    // Avoid thrashing: if already running on the same device and layout, keep it
    if (m_monitoring && m_capture->isRunning() && m_monitorSourceName == src
        && m_capture->format().channelCount() == ch) {
        m_capture->setGain(m_inputVolumeSlider->value() / 100.0);
        if (m_inputMeter)
            m_inputMeter->setStereo(ch >= 2);
        return;
    }
    stopMonitoring();
    m_capture->setGain(m_inputVolumeSlider->value() / 100.0);
    m_capture->setRecording(false);
    if (!m_capture->start(src, 48000, ch)) {
        statusBar()->showMessage(tr("Could not open capture device"), 3000);
        return;
    }
    m_monitorSourceName = src;
    m_monitoring = true;
    if (m_inputMeter)
        m_inputMeter->setStereo(ch >= 2);
}

void MainWindow::stopMonitoring()
{
    if (m_capture) {
        m_capture->setRecording(false);
        m_capture->stop();
    }
    m_monitoring = false;
    m_monitorSourceName.clear();
    if (m_inputMeter) {
        if (m_inputMeter->isStereo())
            m_inputMeter->setLevels(0.0, 0.0);
        else
            m_inputMeter->setLevel(0.0);
    }
}

void MainWindow::onCaptureError(const QString &msg)
{
    QMessageBox::critical(this, tr("Capture Error"), msg);
    if (m_state == AppState::Recording)
        onRecord();
}


void MainWindow::updateInsertPreviewWaveform()
{
    // Composite: left of insert point | growing capture | right of insert point
    const int targetBins = peakBinCount();
    const qint64 liveMs = qMax(qint64(1), m_recordTimer.elapsed());
    const qint64 baseMs = qMax(qint64(1), m_insertBaseDurationMs);
    const qint64 totalMs = baseMs + liveMs;

    int leftBins = int(qreal(m_insertAtMs) / totalMs * targetBins);
    int liveBins = int(qreal(liveMs) / totalMs * targetBins);
    int rightBins = targetBins - leftBins - liveBins;
    if (leftBins < 0)
        leftBins = 0;
    if (liveBins < 1)
        liveBins = 1;
    if (rightBins < 0) {
        liveBins += rightBins;
        rightBins = 0;
    }

    QVector<float> out;
    out.reserve(targetBins);

    const int nBase = m_insertBasePeaks.size();
    const qreal split = (baseMs > 0) ? qreal(m_insertAtMs) / baseMs : 0.0;
    const int splitIdx = qBound(0, int(split * nBase), nBase);

    auto sampleBase = [&](int from, int to, int bins) {
        if (bins <= 0 || nBase <= 0 || to <= from)
            return;
        for (int i = 0; i < bins; ++i) {
            const int idx = from + int(qreal(i) / bins * (to - from));
            out.append(m_insertBasePeaks.at(qBound(0, idx, nBase - 1)));
        }
    };

    sampleBase(0, splitIdx, leftBins);

    // Live insert peaks from full capture PCM (same algorithm as document load)
    if (!m_recordPcm.isEmpty() && liveBins > 0) {
        QAudioFormat liveFmt;
        liveFmt.setSampleFormat(QAudioFormat::Int16);
        liveFmt.setSampleRate(48000);
        liveFmt.setChannelCount(qMax(1, m_recordChannelCount));
        const QVector<float> livePeaks = WavFile::peaks(m_recordPcm, liveFmt, liveBins);
        if (!livePeaks.isEmpty()) {
            for (float v : livePeaks)
                out.append(v);
            // peaks() may return fewer bins for very short audio
            while (out.size() < leftBins + liveBins)
                out.append(0.f);
        } else {
            for (int i = 0; i < liveBins; ++i)
                out.append(0.f);
        }
    } else {
        for (int i = 0; i < liveBins; ++i)
            out.append(0.f);
    }

    sampleBase(splitIdx, nBase, rightBins);

    while (out.size() < targetBins)
        out.append(0.f);
    if (out.size() > targetBins)
        out.resize(targetBins);

    m_rawPeaks = out;
    m_waveform->setPeaks(m_autoScaleWaveform ? normalizedPeaks(m_rawPeaks) : m_rawPeaks);

    // Highlight the growing insert region as A–B
    const qreal a = qreal(m_insertAtMs) / totalMs;
    const qreal b = qreal(m_insertAtMs + liveMs) / totalMs;
    m_waveform->setSelection(a, b);
}

void MainWindow::onMeterTick()
{
    if (!m_capture)
        return;

    // Input meter from lock-free peaks (no per-buffer GUI events)
    if (m_monitoring || m_state == AppState::Recording) {
        if (m_inputMeter->isStereo())
            m_inputMeter->setLevels(m_capture->currentPeakLeft(), m_capture->currentPeakRight());
        else
            m_inputMeter->setLevel(m_capture->currentPeak());
    } else if (m_inputMeter->isStereo()) {
        m_inputMeter->setLevels(0.0, 0.0);
    } else {
        m_inputMeter->setLevel(0.0);
    }

    // Drain PCM while recording
    if (m_state == AppState::Recording && m_wavWriter.isOpen()) {
        const QByteArray pcm = m_capture->takeRecordedAudio();
        if (!pcm.isEmpty()) {
            m_recordPcm.append(pcm);
            m_wavWriter.write(pcm.constData(), pcm.size());
            // Peak bins track widget width so maximized windows gain detail
            // (same as setWaveformFromPcm). Full-PCM scan is O(n) either way.
            QAudioFormat liveFmt;
            liveFmt.setSampleFormat(QAudioFormat::Int16);
            liveFmt.setSampleRate(48000);
            liveFmt.setChannelCount(qMax(1, m_recordChannelCount));
            if (m_insertRecord && !m_insertBasePeaks.isEmpty()) {
                m_liveRecordPeaks = WavFile::peaks(m_recordPcm, liveFmt, qMax(32, peakBinCount() / 2));
                updateInsertPreviewWaveform();
            } else {
                // Same analysis as setWaveformFromPcm so live intensity matches post-stop.
                // Peaks were already a full-PCM scan each drain; density is the same order.
                const WavFile::ChannelPeaks ch = WavFile::channelPeaks(m_recordPcm, liveFmt, peakBinCount());
                m_rawPeaks = WavFile::peaks(m_recordPcm, liveFmt, peakBinCount());
                applyPeaksToWaveform(ch.left, ch.right);

                float densScale = 1.f;
                if (m_autoScaleWaveform) {
                    float mx = 0.f;
                    for (float v : m_rawPeaks)
                        mx = qMax(mx, v);
                    if (mx > 1e-6f)
                        densScale = 1.f / mx;
                }
                const int timeBins = qBound(64, peakBinCount(), 4096);
                const int ampBins = qBound(64, m_waveform ? m_waveform->height() : 128, 512);
                m_waveform->setWaveformDensity(
                    WavFile::waveformDensity(m_recordPcm, liveFmt, timeBins, ampBins, densScale));
                m_waveform->setWaveformDensityAbs(
                    WavFile::waveformDensityAbs(m_recordPcm, liveFmt, timeBins, ampBins, densScale));
            }
        }
        if (m_insertRecord && !m_insertBasePeaks.isEmpty()) {
            const qint64 liveMs = m_recordTimer.elapsed();
            const qint64 totalMs = qMax(qint64(1), m_insertBaseDurationMs + liveMs);
            m_duration = totalMs;
            m_timeLabel->setText(tr("%1 / %2")
                .arg(formatTime(m_insertAtMs + liveMs), formatTime(totalMs)));
            m_seekSlider->setRange(0, int(totalMs));
            m_seekSlider->setValue(int(m_insertAtMs + liveMs));
            m_waveform->setPlaybackPosition(qreal(m_insertAtMs + liveMs) / totalMs);
        } else {
            m_timeLabel->setText(formatTime(m_recordTimer.elapsed()));
            m_duration = m_recordTimer.elapsed();
        }
    }

    // Output meter is updated from onPlayerPosition while playing
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
    Q_UNUSED(start);
    Q_UNUSED(end);
    applySelectionToPlayer();
    updateEditActions();
    updateSelectionLabel();
}

void MainWindow::updateSelectionLabel()
{
    if (!m_selectionLabel)
        return;
    if (m_state == AppState::Recording) {
        // Insert preview owns the selection highlight; keep the label quiet
        if (!m_insertRecord) {
            m_selectionLabel->clear();
            m_selectionLabel->setVisible(false);
        }
        return;
    }
    if (!m_waveform || !m_waveform->hasSelection() || m_duration <= 0) {
        m_selectionLabel->clear();
        m_selectionLabel->setVisible(false);
        return;
    }
    const qint64 a = qint64(m_waveform->selectionStart() * m_duration);
    const qint64 b = qint64(m_waveform->selectionEnd() * m_duration);
    const qint64 len = qMax(qint64(0), b - a);
    m_selectionLabel->setText(
        tr("A–B  %1 – %2   (%3)")
            .arg(formatTime(a), formatTime(b), formatTime(len)));
    m_selectionLabel->setVisible(true);
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
        // Checked only while actually playing — pause must not look "latched".
        m_playAction->setChecked(true);
        m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-pause"), QStyle::SP_MediaPause));
        m_playAction->setText(tr("Pause"));
        m_playAction->setStatusTip(tr("Pause playback"));
        break;
    case PulsePlayback::Paused:
        setAppState(AppState::Paused);
        m_playAction->setChecked(false);
        m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
        m_playAction->setText(tr("Play"));
        m_playAction->setStatusTip(tr("Resume playback"));
        break;
    case PulsePlayback::Stopped:
        if (m_state != AppState::Recording) {
            // Take switch under active play: activateHistoryTake will call play() next.
            if (m_resumePlayAfterTakeLoad)
                break;
            setAppState(AppState::Ready);
            m_playAction->setChecked(false);
            m_playAction->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
            m_playAction->setText(tr("Play"));
            m_playAction->setStatusTip(tr("Play or pause"));
            m_seekSlider->setValue(0);
            m_waveform->setPlaybackPosition(0.0);
            updateTimeLabel();
            if (m_outputMeter->isStereo())
                m_outputMeter->setLevels(0.0, 0.0);
            else
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
        if (m_state == AppState::Playing && m_outputMeter) {
            const qreal vol = m_outputVolumeSlider->value() / 100.0;
            qreal l = 0.0, r = 0.0;
            m_player->levelsAtPosition(ms, &l, &r);
            if (m_outputMeter->isStereo())
                m_outputMeter->setLevels(l * vol, r * vol);
            else
                m_outputMeter->setLevel(qMax(l, r) * vol);
        }
    }
}

void MainWindow::onPlayerDuration(qint64 ms)
{
    m_duration = ms;
    m_seekSlider->setRange(0, static_cast<int>(ms));
    updateTimeLabel();
    updateSelectionLabel();
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
    const bool playing = (m_state == AppState::Playing);
    const bool paused = (m_state == AppState::Paused);
    const bool recording = (m_state == AppState::Recording);
    const bool hasDoc = hasDocument();
    // Idle-ish: can start a new take (record stops playback first if needed)
    const bool canStartRecord = ready || playing || paused;

    // New/Open allowed whenever not recording (including while paused)
    m_newAction->setEnabled(!recording);
    m_openAction->setEnabled(!recording);
    m_saveAction->setEnabled(!recording && (m_modified || hasDoc));
    m_saveAsAction->setEnabled(!recording && hasDoc);
    // Take navigation works while playing: new take keeps playing from the start.
    m_undoAction->setEnabled(!recording && m_history.canPrevious());
    m_redoAction->setEnabled(!recording && m_history.canNext());
    m_historyAction->setEnabled(true); // panel may stay open; load is gated while recording
    m_normalizeAction->setEnabled((ready || paused) && hasDoc);
    // Record is a latch while recording; otherwise always available so pause
    // cannot trap the user (starting record stops playback first).
    m_recordAction->setEnabled(canStartRecord || recording);
    m_playAction->setEnabled((ready || playing || paused) && hasDoc && !recording);
    m_stopAction->setEnabled(playing || paused || recording);
    m_inputCombo->setEnabled(!recording);
    m_outputCombo->setEnabled(!recording);
    // Seek whenever a document is loaded (scrub before/while paused play)
    m_seekSlider->setEnabled(hasDoc && !recording && m_duration > 0);
    updateEditActions();
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
        // Prefer stable filename over list ordinal (ordinals shift on delete).
        if (m_history.takes().size() > 0 && m_history.currentIndex() >= 0)
            name = tr("%1 (cache)").arg(QFileInfo(m_tempPath).fileName());
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
    if (ms < 0)
        ms = 0;
    const int totalSecs = static_cast<int>(ms / 1000);
    const int mins = totalSecs / 60;
    const int secs = totalSecs % 60;
    const int millis = static_cast<int>(ms % 1000);
    return QStringLiteral("%1:%2.%3")
        .arg(mins, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

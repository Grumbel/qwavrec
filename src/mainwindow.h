// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStyle>
#include <QIcon>
#include <QVector>
#include <QList>
#include <QElapsedTimer>
#include <QTimer>
#include <QAudioFormat>

#include "wavwriter.h"
#include "recordinghistory.h"
#include "pulsebackend.h"

class TakesPanel;
class QDockWidget;

class QComboBox;
class QLabel;
class QSlider;
class QAction;
class QActionGroup;
class LevelMeter;
class WaveformWidget;
class MarkedSlider;
class SeekSlider;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshDevices();
    void onPulseDevicesChanged();
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
    void onStop();
    void onNormalize();
    void onCut();
    void onCopy();
    void onPaste();
    void onDeleteSelection();
    void onCropToSelection();
    void onEditUndo();
    void onEditRedo();
    void onWaveformContextMenu(const QPoint &globalPos);
    void onInsertRecordToggled(bool on);
    void onLoopToggled(bool on);
    void onAutoScaleWaveformToggled(bool on);
    void onViewModeTriggered(QAction *action);
    void onAbout();
    void onUndo();
    void onRedo();
    void onHistory(bool show);
    void onTakesLoadRequested(int index);
    void onTakesDeleteRequested(const QList<int> &indices);

    void onPlayerStateChanged(PulsePlayback::State state);
    void onPlayerPosition(qint64 ms);
    void onPlayerDuration(qint64 ms);
    void onPlayerError(const QString &msg);
    void onSeek(int value);
    void onWaveformSeek(qreal pos);
    void onSelectionChanged(qreal start, qreal end);
    void onWaveformWidthChanged(int width);

    void onCaptureError(const QString &msg);
    void onMeterTick();

private:
    enum class AppState { Ready, Playing, Paused, Recording, Error };

    void createActions();
    void createMenus();
    void createToolBar();
    void createCentralWidget();
    void setAppState(AppState state);
    void updateControls();
    void updateTimeLabel();
    void updateSelectionLabel();
    void updateWindowTitle();
    void updateMicGainLabel();
    void updatePlaybackVolumeLabel();
    QString formatTime(qint64 ms) const;
    QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback) const;

    void startMonitoring();
    void stopMonitoring();
    void finishRecordingStop();
    void refreshTakesPanel();
    void loadTakeAtIndex(int index);
    /** Load a cache take; if Playing, resume from the start of the new take. */
    void activateHistoryTake(const QString &path);
    void applySelectionToPlayer();

    bool maybeSave();
    void clearDocument();
    QString documentPathForPlayback() const;
    bool hasDocument() const;
    void markModified();
    void loadDocumentForPlayback(const QString &path);
    void setWaveformFromPcm(const QByteArray &pcm, const QAudioFormat &fmt);
    int peakBinCount() const;
    void rebuildPeaksFromDocument();
    void applyDisplayMode();
    QVector<float> normalizedPeaks(const QVector<float> &raw) const;

    void loadSettings();
    void saveSettings();
    bool normalizeCurrentFile();
    void pushEditUndo(const QString &label);
    void applyDocumentPcm(const QByteArray &pcm, const QAudioFormat &fmt);
    bool writeDocumentPcm(const QByteArray &pcm, const QAudioFormat &fmt);
    QByteArray selectionPcm(const QByteArray &pcm, const QAudioFormat &fmt,
                           qreal a, qreal b) const;
    void updateEditActions();

    QString currentSourceName() const;
    QString currentSinkName() const;

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_recordAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_normalizeAction = nullptr;
    QAction *m_loopAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_autoScaleAction = nullptr;
    QAction *m_waveformViewAction = nullptr;
    QAction *m_spectrogramAction = nullptr;
    QActionGroup *m_viewModeGroup = nullptr;
    QAction *m_historyAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_deleteSelAction = nullptr;
    QAction *m_cropAction = nullptr;
    QAction *m_editUndoAction = nullptr;
    QAction *m_editRedoAction = nullptr;
    QAction *m_insertRecordAction = nullptr;

    QComboBox *m_inputCombo = nullptr;
    QComboBox *m_outputCombo = nullptr;
    MarkedSlider *m_inputVolumeSlider = nullptr;
    QLabel *m_micGainLabel = nullptr;
    QSlider *m_outputVolumeSlider = nullptr;
    QLabel *m_playbackVolumeLabel = nullptr;
    SeekSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;
    LevelMeter *m_inputMeter = nullptr;
    LevelMeter *m_outputMeter = nullptr;
    WaveformWidget *m_waveform = nullptr;
    QDockWidget *m_takesDock = nullptr;
    TakesPanel *m_takesPanel = nullptr;

    PulseCapture *m_capture = nullptr;
    PulsePlayback *m_player = nullptr;
    RecordingHistory m_history;
    WavWriter m_wavWriter;

    QVector<float> m_liveRecordPeaks;
    QVector<float> m_rawPeaks;
    QByteArray m_recordPcm; // PCM accumulated during current take (GUI thread)
    QByteArray m_clipPcm;
    QAudioFormat m_clipFormat;
    struct EditSnap { QByteArray pcm; QAudioFormat format; QString label; };
    QVector<EditSnap> m_editUndo;
    QVector<EditSnap> m_editRedo;
    static const int kMaxEditUndo = 20;
    bool m_insertRecord = false;
    qint64 m_insertAtMs = 0;
    QByteArray m_insertBasePcm;
    QAudioFormat m_insertBaseFormat;
    QVector<float> m_insertBasePeaks;
    qint64 m_insertBaseDurationMs = 0;
    QString m_captureTempPath;
    void updateInsertPreviewWaveform();

    QString m_savedPath;
    QString m_tempPath;
    bool m_isTemporary = true;
    bool m_modified = false;

    AppState m_state = AppState::Ready;
    qint64 m_duration = 0;
    bool m_seeking = false;
    QElapsedTimer m_recordTimer;

    QString m_pendingInputName;
    QString m_pendingOutputName;
    bool m_restoringSettings = false;
    bool m_autoScaleWaveform = false;
    bool m_spectrogramMode = false;
    /** Skip Stopped→Ready side effects while switching takes under playback. */
    bool m_resumePlayAfterTakeLoad = false;
    bool m_monitoring = false;
    /** Source name currently opened for the input meter (empty = server default). */
    QString m_monitorSourceName;
    QTimer *m_meterTimer = nullptr;
    /** Event-driven source/sink list (Pulse subscription on a worker thread). */
    class PulseDeviceWatcher *m_deviceWatcher = nullptr;
    /** Debounce hotplug storms before rebuilding combos. */
    QTimer *m_deviceDebounceTimer = nullptr;
    QTimer *m_waveformResizeTimer = nullptr;
};

#endif

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RECORDINGHISTORY_H
#define RECORDINGHISTORY_H

#include <QString>
#include <QStringList>
#include <QDir>

/**
 * Keeps every finished take under $XDG_CACHE_HOME/qwavrec/ (or
 * QStandardPaths::CacheLocation) and supports undo/redo navigation.
 */
class RecordingHistory
{
public:
    RecordingHistory();

    QString cacheDir() const { return m_dir; }

    /** Copy a finished recording into the cache; returns the new path. */
    QString archiveTake(const QString &sourcePath);

    /** Paths of archived takes, oldest first. */
    QStringList takes() const { return m_takes; }

    int currentIndex() const { return m_index; }
    QString currentPath() const;

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index >= 0 && m_index < m_takes.size() - 1; }

    /** Move to previous take; returns path or empty. */
    QString undo();
    /** Move to next take; returns path or empty. */
    QString redo();

    /** Jump to latest take after a new archive. */
    void selectLatest();

    /** Rescan the cache directory. */
    void reload();

private:
    QString m_dir;
    QStringList m_takes;
    int m_index = -1;
};

#endif

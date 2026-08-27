// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RECORDINGHISTORY_H
#define RECORDINGHISTORY_H

#include <QString>
#include <QStringList>
#include <QDir>

/**
 * Keeps finished takes under $XDG_CACHE_HOME/qwavrec/ and supports
 * previous/next navigation. Oldest files are pruned when over the limit.
 */
class RecordingHistory
{
public:
    static constexpr int DefaultMaxTakes = 50;

    RecordingHistory();

    QString cacheDir() const { return m_dir; }

    /** Copy a finished recording into the cache; returns the new path. */
    QString archiveTake(const QString &sourcePath);

    QStringList takes() const { return m_takes; }
    int currentIndex() const { return m_index; }
    QString currentPath() const;

    bool canPrevious() const { return m_index > 0; }
    bool canNext() const { return m_index >= 0 && m_index < m_takes.size() - 1; }

    QString previous();
    QString next();
    void selectLatest();
    bool selectIndex(int index);
    bool removeAt(int index);
    void reload();

    void setMaxTakes(int n);
    int maxTakes() const { return m_maxTakes; }

private:
    void prune();

    QString m_dir;
    QStringList m_takes;
    int m_index = -1;
    int m_maxTakes = DefaultMaxTakes;
};

#endif

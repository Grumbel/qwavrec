// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "recordinghistory.h"

#include <QStandardPaths>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

RecordingHistory::RecordingHistory()
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (m_dir.isEmpty())
        m_dir = QDir::homePath() + QStringLiteral("/.cache");
    m_dir += QStringLiteral("/qwavrec");
    QDir().mkpath(m_dir);
    reload();
}

void RecordingHistory::reload()
{
    m_takes.clear();
    QDir d(m_dir);
    const auto files = d.entryList({QStringLiteral("rec-*.wav")}, QDir::Files, QDir::Name);
    for (const QString &f : files)
        m_takes.append(d.absoluteFilePath(f));
    if (m_takes.isEmpty())
        m_index = -1;
    else if (m_index < 0 || m_index >= m_takes.size())
        m_index = m_takes.size() - 1;
}

void RecordingHistory::setMaxTakes(int n)
{
    m_maxTakes = qMax(1, n);
    prune();
}

void RecordingHistory::prune()
{
    while (m_takes.size() > m_maxTakes) {
        const QString old = m_takes.takeFirst();
        QFile::remove(old);
        if (m_index > 0)
            --m_index;
        else if (m_index == 0 && !m_takes.isEmpty())
            m_index = 0;
    }
    if (m_takes.isEmpty())
        m_index = -1;
}

QString RecordingHistory::archiveTake(const QString &sourcePath)
{
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath))
        return {};

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString dest = m_dir + QStringLiteral("/rec-") + stamp + QStringLiteral(".wav");

    if (QFile::exists(dest))
        QFile::remove(dest);
    if (!QFile::copy(sourcePath, dest))
        return {};

    m_takes.append(dest);
    m_index = m_takes.size() - 1;
    prune();
    // After prune, index may need clamp
    if (!m_takes.isEmpty())
        m_index = m_takes.size() - 1;
    return m_takes.isEmpty() ? QString() : m_takes.last();
}

QString RecordingHistory::currentPath() const
{
    if (m_index < 0 || m_index >= m_takes.size())
        return {};
    return m_takes.at(m_index);
}

QString RecordingHistory::previous()
{
    if (!canPrevious())
        return {};
    --m_index;
    return currentPath();
}

QString RecordingHistory::next()
{
    if (!canNext())
        return {};
    ++m_index;
    return currentPath();
}

void RecordingHistory::selectLatest()
{
    if (m_takes.isEmpty())
        m_index = -1;
    else
        m_index = m_takes.size() - 1;
}

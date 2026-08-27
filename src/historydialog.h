// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>
#include <QStringList>

class QListWidget;
class QLabel;

class HistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HistoryDialog(const QStringList &takes, int currentIndex,
                           const QString &cacheDir, QWidget *parent = nullptr);

    /** Index chosen to load, or -1 if none / cancelled. */
    int selectedIndex() const { return m_selected; }
    /** True if the user asked to delete the selected take. */
    bool deleteRequested() const { return m_delete; }

private slots:
    void onLoad();
    void onDelete();
    void onSelectionChanged();

private:
    void refreshDetails();

    QListWidget *m_list = nullptr;
    QLabel *m_details = nullptr;
    QStringList m_takes;
    int m_selected = -1;
    bool m_delete = false;
};

#endif

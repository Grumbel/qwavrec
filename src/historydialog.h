// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QWidget>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;

/**
 * Persistent take list for the main window (embedded in a QDockWidget).
 * Not a modal dialog — stays open for fast switching between takes.
 */
class TakesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TakesPanel(QWidget *parent = nullptr);

    void setCacheDir(const QString &dir);
    /** Rebuild the list; highlights currentIndex without emitting loadRequested. */
    void setTakes(const QStringList &takes, int currentIndex);

signals:
    void loadRequested(int index);
    void deleteRequested(int index);

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onCurrentRowChanged(int row);
    void onDeleteClicked();

private:
    void updateDetails();

    QLabel *m_header = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_details = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QStringList m_takes;
    bool m_blockLoad = false;
};

#endif

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "historydialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>

HistoryDialog::HistoryDialog(const QStringList &takes, int currentIndex,
                             const QString &cacheDir, QWidget *parent)
    : QDialog(parent)
    , m_takes(takes)
{
    setWindowTitle(tr("Take History"));
    setMinimumSize(480, 360);
    resize(520, 420);

    auto *layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(
        tr("Cached takes in %1 (newest at the bottom):").arg(cacheDir)));

    m_list = new QListWidget;
    m_list->setAlternatingRowColors(true);
    for (int i = 0; i < takes.size(); ++i) {
        const QFileInfo fi(takes.at(i));
        const QString stamp = fi.birthTime().isValid()
            ? fi.birthTime().toString(Qt::DefaultLocaleShortDate)
            : fi.lastModified().toString(Qt::DefaultLocaleShortDate);
        const qint64 kb = fi.size() / 1024;
        const QString text = tr("%1.  %2  —  %3 KB  —  %4")
            .arg(i + 1)
            .arg(fi.fileName())
            .arg(kb)
            .arg(stamp);
        auto *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, i);
        m_list->addItem(item);
    }
    if (currentIndex >= 0 && currentIndex < m_list->count())
        m_list->setCurrentRow(currentIndex);
    else if (m_list->count() > 0)
        m_list->setCurrentRow(m_list->count() - 1);

    layout->addWidget(m_list, 1);

    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_details);

    auto *buttons = new QDialogButtonBox;
    auto *loadBtn = buttons->addButton(tr("Load"), QDialogButtonBox::AcceptRole);
    auto *delBtn = buttons->addButton(tr("Delete"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(loadBtn, &QPushButton::clicked, this, &HistoryDialog::onLoad);
    connect(delBtn, &QPushButton::clicked, this, &HistoryDialog::onDelete);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &HistoryDialog::onLoad);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { refreshDetails(); });

    loadBtn->setDefault(true);
    delBtn->setEnabled(m_list->count() > 0);
    loadBtn->setEnabled(m_list->count() > 0);
    refreshDetails();
}

void HistoryDialog::refreshDetails()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_takes.size()) {
        m_details->setText(tr("No takes in cache."));
        return;
    }
    const QFileInfo fi(m_takes.at(row));
    m_details->setText(tr("Path: %1\nSize: %2 bytes")
        .arg(fi.absoluteFilePath())
        .arg(fi.size()));
}

void HistoryDialog::onLoad()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    m_selected = m_list->item(row)->data(Qt::UserRole).toInt();
    m_delete = false;
    accept();
}

void HistoryDialog::onDelete()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    const QString name = QFileInfo(m_takes.at(row)).fileName();
    if (QMessageBox::question(this, tr("Delete Take"),
            tr("Permanently delete “%1” from the cache?").arg(name))
        != QMessageBox::Yes)
        return;
    m_selected = m_list->item(row)->data(Qt::UserRole).toInt();
    m_delete = true;
    accept();
}

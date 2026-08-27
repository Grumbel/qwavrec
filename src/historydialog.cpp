// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "historydialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QLocale>

TakesPanel::TakesPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    m_header = new QLabel(tr("Cached takes (newest at the bottom):"));
    m_header->setWordWrap(true);
    layout->addWidget(m_header);

    m_list = new QListWidget;
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_details);

    auto *row = new QHBoxLayout;
    m_deleteBtn = new QPushButton(tr("Delete"));
    m_deleteBtn->setEnabled(false);
    row->addStretch();
    row->addWidget(m_deleteBtn);
    layout->addLayout(row);

    connect(m_list, &QListWidget::itemActivated, this, &TakesPanel::onItemActivated);
    connect(m_list, &QListWidget::currentRowChanged, this, &TakesPanel::onCurrentRowChanged);
    connect(m_deleteBtn, &QPushButton::clicked, this, &TakesPanel::onDeleteClicked);
}

void TakesPanel::setCacheDir(const QString &dir)
{
    m_header->setText(tr("Cached takes in %1\n(newest at the bottom; click to load):").arg(dir));
}

void TakesPanel::setTakes(const QStringList &takes, int currentIndex)
{
    m_takes = takes;
    m_blockLoad = true;
    m_list->clear();
    for (int i = 0; i < takes.size(); ++i) {
        const QFileInfo fi(takes.at(i));
        const QString stamp = fi.birthTime().isValid()
            ? fi.birthTime().toString(QLocale::system().dateTimeFormat(QLocale::ShortFormat))
            : fi.lastModified().toString(QLocale::system().dateTimeFormat(QLocale::ShortFormat));
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
    m_blockLoad = false;
    m_deleteBtn->setEnabled(m_list->currentRow() >= 0);
    updateDetails();
}

void TakesPanel::onItemActivated(QListWidgetItem *item)
{
    if (!item || m_blockLoad)
        return;
    emit loadRequested(item->data(Qt::UserRole).toInt());
}

void TakesPanel::onCurrentRowChanged(int row)
{
    m_deleteBtn->setEnabled(row >= 0);
    updateDetails();
    if (m_blockLoad || row < 0)
        return;
    // Single click / arrow keys load immediately for fast take switching.
    emit loadRequested(row);
}

void TakesPanel::onDeleteClicked()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    emit deleteRequested(row);
}

void TakesPanel::updateDetails()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_takes.size()) {
        m_details->setText(m_takes.isEmpty() ? tr("No takes in cache.") : QString());
        return;
    }
    const QFileInfo fi(m_takes.at(row));
    m_details->setText(tr("%1\n%2 KB")
        .arg(fi.absoluteFilePath())
        .arg(fi.size() / 1024));
}

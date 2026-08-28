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
#include <QKeySequence>
#include <algorithm>

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
    // Ctrl/Shift multi-select for batch delete; single selection still loads.
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformItemSizes(true);
    layout->addWidget(m_list, 1);

    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_details);

    auto *row = new QHBoxLayout;
    m_deleteBtn = new QPushButton(tr("Delete"));
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setToolTip(tr("Delete selected takes from the cache (Del)"));
    m_deleteBtn->setShortcut(QKeySequence::Delete);
    row->addStretch();
    row->addWidget(m_deleteBtn);
    layout->addLayout(row);

    connect(m_list, &QListWidget::itemActivated, this, &TakesPanel::onItemActivated);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &TakesPanel::onSelectionChanged);
    connect(m_deleteBtn, &QPushButton::clicked, this, &TakesPanel::onDeleteClicked);
}

void TakesPanel::setCacheDir(const QString &dir)
{
    m_header->setText(
        tr("Cached takes in %1\n"
           "(newest at the bottom; click to load, Ctrl/Shift to multi-select):")
            .arg(dir));
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
        // No list ordinal — it shifts on every delete. Identity is the filename
        // (rec-YYYYMMDD-HHMMSS-zzz.wav) plus size and time.
        const QString text = tr("%1  —  %2 KB  —  %3")
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
    onSelectionChanged();
}

QList<int> TakesPanel::selectedIndices() const
{
    QList<int> indices;
    const QList<QListWidgetItem *> items = m_list->selectedItems();
    indices.reserve(items.size());
    for (QListWidgetItem *item : items) {
        if (item)
            indices.append(item->data(Qt::UserRole).toInt());
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

void TakesPanel::onItemActivated(QListWidgetItem *item)
{
    if (!item || m_blockLoad)
        return;
    emit loadRequested(item->data(Qt::UserRole).toInt());
}

void TakesPanel::onSelectionChanged()
{
    const QList<int> indices = selectedIndices();
    const int n = indices.size();
    m_deleteBtn->setEnabled(n > 0);
    if (n <= 1)
        m_deleteBtn->setText(tr("Delete"));
    else
        m_deleteBtn->setText(tr("Delete (%1)").arg(n));

    updateDetails();

    if (m_blockLoad || n != 1)
        return;
    // Single selection: load immediately (click / arrow keys). Multi-select
    // is for batch delete only — do not thrash-load every Ctrl+clicked take.
    emit loadRequested(indices.first());
}

void TakesPanel::onDeleteClicked()
{
    const QList<int> indices = selectedIndices();
    if (indices.isEmpty())
        return;
    emit deleteRequested(indices);
}

void TakesPanel::updateDetails()
{
    const QList<int> indices = selectedIndices();
    if (indices.isEmpty()) {
        m_details->setText(m_takes.isEmpty() ? tr("No takes in cache.") : tr("No selection."));
        return;
    }
    if (indices.size() == 1) {
        const int row = indices.first();
        if (row < 0 || row >= m_takes.size()) {
            m_details->clear();
            return;
        }
        const QFileInfo fi(m_takes.at(row));
        m_details->setText(tr("%1\n%2 KB")
            .arg(fi.absoluteFilePath())
            .arg(fi.size() / 1024));
        return;
    }

    qint64 totalBytes = 0;
    for (int row : indices) {
        if (row >= 0 && row < m_takes.size())
            totalBytes += QFileInfo(m_takes.at(row)).size();
    }
    m_details->setText(tr("%1 takes selected\n%2 KB total")
        .arg(indices.size())
        .arg(totalBytes / 1024));
}

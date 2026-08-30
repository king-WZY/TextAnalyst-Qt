// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// FilterDockWidget.cpp: 紧凑规则列表面板（R-22：编辑表单移入弹窗）
// =============================================================================

#include "ui/widgets/FilterDockWidget.h"

#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>

#include "ui/widgets/FilterListModel.h"

namespace tat {

FilterDockWidget::FilterDockWidget(QWidget* parent)
    : QDockWidget(QStringLiteral("过滤规则"), parent) {
    auto* root = new QWidget(this);
    auto* vbox = new QVBoxLayout(root);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);

    // 纯列表（R-23：规则操作入口在菜单栏 Filters 菜单，本面板无按钮）
    m_table = new QTableView(root);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vbox->addWidget(m_table, 1);

    setWidget(root);

    connect(m_table, &QTableView::doubleClicked, this,
            [this](const QModelIndex& idx) {
                if (idx.isValid()) emit ruleDoubleClicked(idx.row());
            });
}

void FilterDockWidget::setModel(FilterListModel* model) {
    m_model = model;
    m_table->setModel(model);
    m_table->setColumnWidth(FilterListModel::ColEnabled, 36);
    m_table->setColumnWidth(FilterListModel::ColColor, 36);
    m_table->setColumnWidth(FilterListModel::ColMode, 56);
    m_table->setColumnWidth(FilterListModel::ColPattern, 220);
    m_table->horizontalHeader()->setStretchLastSection(true);
    connect(model, &QAbstractItemModel::dataChanged, this,
            &FilterDockWidget::rulesListChanged);
    connect(model, &QAbstractItemModel::modelReset, this,
            &FilterDockWidget::rulesListChanged);
    connect(model, &QAbstractItemModel::rowsInserted, this,
            &FilterDockWidget::rulesListChanged);
    connect(model, &QAbstractItemModel::rowsRemoved, this,
            &FilterDockWidget::rulesListChanged);
}

}  // namespace tat
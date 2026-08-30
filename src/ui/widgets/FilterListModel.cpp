// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// FilterListModel.cpp
// =============================================================================

#include "ui/widgets/FilterListModel.h"

#include "core/engine/FilterResult.h"

namespace tat {

FilterListModel::FilterListModel(QObject* parent) : QAbstractTableModel(parent) {}

int FilterListModel::rowCount(const QModelIndex&) const {
    return m_rules.size();
}
int FilterListModel::columnCount(const QModelIndex&) const { return ColumnCount; }

QVariant FilterListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rules.size()) return {};
    const FilterRule& r = m_rules.at(index.row());

    switch (index.column()) {
    case ColEnabled:
        if (role == Qt::CheckStateRole)
            return r.isEnabled ? Qt::Checked : Qt::Unchecked;
        break;
    case ColColor:
        if (role == Qt::BackgroundRole)
            return QColor(QRgb(r.background));
        if (role == Qt::ForegroundRole)
            return QColor(QRgb(r.foreground));
        if (role == Qt::DisplayRole)
            return QStringLiteral("▮");
        break;
    case ColMode:
        if (role == Qt::DisplayRole)
            return r.mode == MatchMode::Regex ? QStringLiteral("Regex")
                                              : QStringLiteral("含");
        break;
    case ColPattern:
        if (role == Qt::DisplayRole) {
            QString p = QString::fromStdString(r.pattern);
            if (p.size() > 48) p = p.left(48) + QStringLiteral("…");
            return p;
        }
        if (role == Qt::ToolTipRole)
            return QString::fromStdString(r.pattern);
        break;
    case ColDesc:
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            return QString::fromStdString(r.description);
        }
        break;
    case ColCount:
        if (role == Qt::DisplayRole) return r.matchCount;
        break;
    }
    return {};
}

QVariant FilterListModel::headerData(int section, Qt::Orientation o,
                                     int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColEnabled: return QStringLiteral("开");
    case ColColor:   return QStringLiteral("色");
    case ColMode:    return QStringLiteral("模式");
    case ColPattern: return QStringLiteral("过滤条件");
    case ColDesc:    return QStringLiteral("备注");
    case ColCount:   return QStringLiteral("命中");
    }
    return {};
}

Qt::ItemFlags FilterListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (index.column() == ColEnabled) f |= Qt::ItemIsUserCheckable;
    if (index.column() == ColPattern) f |= Qt::ItemIsEditable;
    return f;
}

bool FilterListModel::setData(const QModelIndex& index, const QVariant& value,
                              int role) {
    if (!index.isValid() || index.row() >= m_rules.size()) return false;
    FilterRule& r = m_rules[index.row()];
    if (index.column() == ColEnabled && role == Qt::CheckStateRole) {
        r.isEnabled = value.toInt() == Qt::Checked;
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    if (index.column() == ColPattern && role == Qt::EditRole) {
        r.pattern = value.toString().toStdString();
        emit dataChanged(index, index, {Qt::DisplayRole});
        return true;
    }
    return false;
}

void FilterListModel::addRule(const FilterRule& rule) {
    beginInsertRows({}, m_rules.size(), m_rules.size());
    m_rules.append(rule);
    endInsertRows();
}

void FilterListModel::enableAll(bool enabled) {
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].isEnabled != enabled) {
            m_rules[i].isEnabled = enabled;
            emit dataChanged(index(i, 0), index(i, ColumnCount - 1));
        }
    }
}

void FilterListModel::removeRuleAt(int row) {
    if (row < 0 || row >= m_rules.size()) return;
    beginRemoveRows({}, row, row);
    m_rules.removeAt(row);
    endRemoveRows();
}

void FilterListModel::replaceRuleAt(int row, const FilterRule& rule) {
    if (row < 0 || row >= m_rules.size()) return;
    FilterRule updated = rule;
    updated.id = m_rules[row].id;  // 编辑不改变 id
    m_rules[row] = updated;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void FilterListModel::replaceRuleIdAt(int row, int id) {
    if (row < 0 || row >= m_rules.size()) return;
    if (m_rules[row].id == id) return;
    m_rules[row].id = id;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void FilterListModel::clearRules() {
    beginResetModel();
    m_rules.clear();
    endResetModel();
}

void FilterListModel::refreshMatchCounts(
    const FilterResult& result, const std::vector<int>& /*ignored*/) {
    for (int i = 0; i < m_rules.size(); ++i) {
        const int id = m_rules.at(i).id;
        const int count = (id >= 0 && id < kMaxRules)
                              ? result.matchCounts[static_cast<size_t>(id)].load()
                              : 0;
        if (m_rules[i].matchCount != count) {
            m_rules[i].matchCount = count;
            emit dataChanged(index(i, ColCount), index(i, ColCount),
                             {Qt::DisplayRole});
        }
    }
}

}  // namespace tat
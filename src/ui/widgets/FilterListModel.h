// =============================================================================
// FilterListModel.h: 过滤器规则表格模型（Dock 面板）
// 文档：DISPLAYDESIGN.md §7.5.1
// 列：Enabled / 颜色 / Mode / Pattern / 命中数
// =============================================================================
#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QVector>

#include "core/engine/FilterResult.h"
#include "core/models/FilterRule.h"

namespace tat {

class FilterListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Columns {
        ColEnabled = 0,
        ColColor,
        ColMode,
        ColPattern,
        ColDesc,
        ColCount,
        ColumnCount,
    };

    explicit FilterListModel(QObject* parent = nullptr);

    int  rowCount(const QModelIndex& = {}) const override;
    int  columnCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role) override;

    void addRule(const FilterRule& rule);
    void enableAll(bool enabled);   // 全部启用/禁用（规则保留）
    void removeRuleAt(int row);
    void replaceRuleAt(int row, const FilterRule& rule);  // 编辑弹窗回写（保 id）
    void replaceRuleIdAt(int row, int id);   // 规则 id 分配后回写
    void clearRules();
    QVector<FilterRule> rules() const { return m_rules; }
    FilterRule ruleAt(int row) const { return m_rules.value(row); }

    // 由 MainWindow 在 filterApplied 后调用（用 FilterResult.matchCounts 刷新）
    void refreshMatchCounts(const FilterResult& result,
                            const std::vector<int>& ruleIndexByIdForRow);

private:
    QVector<FilterRule> m_rules;
};

}  // namespace tat
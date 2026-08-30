// =============================================================================
// FilterDockWidget.h: 过滤规则列表面板（底部常驻，紧凑，可关闭/恢复）
// 文档：DISPLAYDESIGN.md §7.5.2（R-22/R-23：编辑表单在 FilterRuleDialog
//       弹窗；规则操作入口在菜单栏 Filters 菜单——本面板是纯列表，
//       无任何操作按钮）
// 交互：
//   - 表格双击规则行 → ruleDoubleClicked(row) → MainWindow 弹出编辑弹窗
//   - 全部规则操作（增删改/启停/跳转）在菜单栏 Filters 菜单（R-23）
// =============================================================================
#pragma once

#include <QDockWidget>

#include "core/models/FilterRule.h"

class QPushButton;
class QTableView;

namespace tat {

class FilterListModel;

class FilterDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit FilterDockWidget(QWidget* parent = nullptr);

    FilterListModel* model() const noexcept { return m_model; }
    void setModel(FilterListModel* model);
    QTableView* tableView() const noexcept { return m_table; }

signals:
    void rulesListChanged();               // 规则集变化（增删/勾选/编辑回写）
    void ruleDoubleClicked(int row);       // 表格双击规则行 → 编辑弹窗

private:
    FilterListModel* m_model = nullptr;
    QTableView*      m_table = nullptr;
};

}  // namespace tat
// =============================================================================
// LogViewModel.h: 只读行模型（虚拟化 + 可见行映射）
// 文档：DISPLAYDESIGN.md §7.2（R-26 修订：Hidden 行折叠不占槽）
//
// 行号语义（重要）：
//   - 视图行（viewRow）：QModelIndex.row()，仅含可见行，0-based
//   - 原始行号（rawLine）：文件中的 1-based 行号（与 TAT 一致，
//     Hidden/仅显示模式下行号仍显示原始值，便于定位）
//
// 映射策略（R-26）：
//   - 无过滤结果（或全部可见）→ 恒等映射（m_identity），零额外内存
//   - 存在 Hidden 行或"仅显示匹配"开启 → m_visibleRows 升序表
//     （1-based 原始行号），视图行 = 映射表下标
// =============================================================================
#pragma once

#include <QAbstractListModel>
#include <QFont>

#include "controller/MainController.h"

namespace tat {

class LogViewModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        LineNoRole = Qt::UserRole + 1,   // 原始行号（1-based，R-26）
        StateRole,                        // RowState.raw（原始行的状态）
        RuleRefRole,                      // ruleRef
        TextRole,                         // 原始行文本
    };

    explicit LogViewModel(MainController* controller, QObject* parent = nullptr);

    // bufferChanged / filterApplied / 仅显示切换后调用：重建可见行映射
    void rebuildFromController();

    // Ctrl+H / 视图菜单："仅显示过滤的行"（只显示着色规则命中的行）
    void setShowMatchedOnly(bool on);
    bool showMatchedOnly() const noexcept { return m_showMatchedOnly; }

    int  rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // 视图行 → 原始行号（越界返回 kInvalidLine）
    int rawLineOf(int viewRow) const noexcept;
    // 原始行号 → 最近可见视图行（跳转用；原始行被隐藏时落在其后的可见行）
    int visibleIndexFor(int rawLine) const noexcept;
    bool isIdentity() const noexcept { return m_identity; }

    int  rowHeight() const noexcept { return m_rowHeight; }
    void setFont(const QFont& f);  // 重算固定行高

    MainController* controller() const noexcept { return m_controller; }

private:
    MainController* m_controller;
    int m_rowHeight = 18;
    QFont m_font;

    std::vector<int> m_visibleRows;  // 原始行号（1-based），升序
    bool m_identity        = true;   // 恒等映射（视图行 == 原始行-1）
    bool m_showMatchedOnly = false;
};

}  // namespace tat
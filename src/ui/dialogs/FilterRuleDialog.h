// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// FilterRuleDialog.h: 过滤规则编辑弹窗（添加 / 修改共用同一窗口）
// 文档：DISPLAYDESIGN.md §7.5.2（R-22 修订：编辑表单弹窗化，不常驻占空间）
//
// 使用方式（MainWindow 装配，本类与列表/控制器零耦合）：
//   添加模式：setRule(预填初值) → open() → accepted 后由调用方读取 rule()
//             追加进列表；editRuleId() == -1
//   修改模式：setRule(现有规则) + setEditMode(ruleId) → accepted 后调用方
//             用 rule() 回写列表对应行；editRuleId() == 该规则 id
//
// 布局（原版 TAT 三行，R-13）：
//   第1行  Filter 下拉 · Text Color 下拉 · Background 下拉
//   第2行  Text: 单行输入
//   第3行  Description: 单行输入
//   底部   [x] Excluding [!]  [x] Case-sensitive [Aa]  [x] Regular expression [R]
//          确定 / 取消（QDialogButtonBox）
// =============================================================================
#pragma once

#include <QDialog>

#include "core/models/FilterRule.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace tat {

class FilterRuleDialog : public QDialog {
    Q_OBJECT
public:
    explicit FilterRuleDialog(QWidget* parent = nullptr);

    // 预填表单（Add 模式传初值/默认值；Edit 模式传现有规则）
    void setRule(const FilterRule& rule);
    // 读取表单为一条规则（id 字段 = editRuleId 设置的值，Add 模式为 -1）
    FilterRule rule() const;

    // 进入"修改"模式：标题与确定按钮文案变化，rule().id 返回该 id；
    // 不调用（或传 -1）则为"添加"模式。
    void setEditMode(int ruleId);
    int  editRuleId() const noexcept { return m_editId; }

private:
    void populateColorCombo(QComboBox* combo, bool includeNone);

    int        m_editId = -1;    // -1 = 添加模式
    QComboBox* m_filterCombo = nullptr;
    QComboBox* m_fgCombo     = nullptr;
    QComboBox* m_bgCombo     = nullptr;
    QLineEdit* m_textEdit    = nullptr;
    QLineEdit* m_descEdit    = nullptr;
    QCheckBox* m_excludeCheck = nullptr;
    QCheckBox* m_caseCheck    = nullptr;
    QCheckBox* m_regexCheck   = nullptr;
};

}  // namespace tat
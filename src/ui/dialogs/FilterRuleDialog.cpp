// =============================================================================
// FilterRuleDialog.cpp: 规则编辑弹窗实现
// =============================================================================

#include "ui/dialogs/FilterRuleDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace tat {

namespace {

struct ColorPreset {
    const char* name;
    Argb        value;
};
const ColorPreset kForegroundColors[] = {
    {"Black", 0xFF000000}, {"Red", 0xFFFF5555},  {"Green", 0xFF55AA55},
    {"Blue", 0xFF5588FF},  {"Yellow", 0xFF984700}, {"Orange", 0xFFFFA500},
    {"Gray", 0xFF999999},  {"White", 0xFFFFFFFF},
};
const ColorPreset kBackgroundColors[] = {
    {"None", 0x00000000},   {"Black", 0xFF404040}, {"White", 0xFFFFFFFF},
    {"Light Gray", 0xFFD7D7D7}, {"Red", 0xFFFF8080}, {"Green", 0xFF80CC80},
    {"Yellow", 0xFFF7F779}, {"Blue", 0xFF88AAFF},
};

}  // namespace

FilterRuleDialog::FilterRuleDialog(QWidget* parent) : QDialog(parent) {
    // R-22：Add/Edit 由 setEditMode 区分，窗口构造即确定形态
    setModal(true);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(10);

    // ---- 第 1 行：Filter / Text Color / Background ----
    auto* row1 = new QWidget(this);
    auto* h1 = new QHBoxLayout(row1);
    h1->setContentsMargins(0, 0, 0, 0);
    m_filterCombo = new QComboBox(row1);
    m_filterCombo->addItem(QStringLiteral("Matches text"),
                           static_cast<int>(FilterMatch::Exact));
    m_filterCombo->addItem(QStringLiteral("Contains"),
                           static_cast<int>(FilterMatch::Contains));
    m_filterCombo->addItem(QStringLiteral("Starts with"),
                           static_cast<int>(FilterMatch::StartsWith));
    m_filterCombo->addItem(QStringLiteral("Ends with"),
                           static_cast<int>(FilterMatch::EndsWith));
    m_fgCombo = new QComboBox(row1);
    populateColorCombo(m_fgCombo, /*includeNone=*/false);
    m_fgCombo->setCurrentIndex(1);  // 默认 Red
    m_bgCombo = new QComboBox(row1);
    populateColorCombo(m_bgCombo, /*includeNone=*/true);
    m_bgCombo->setCurrentIndex(3);  // 默认 Light Gray
    h1->addWidget(new QLabel(QStringLiteral("Filter"), row1));
    h1->addWidget(m_filterCombo, 1);
    h1->addWidget(new QLabel(QStringLiteral("Text Color"), row1));
    h1->addWidget(m_fgCombo, 1);
    h1->addWidget(new QLabel(QStringLiteral("Background"), row1));
    h1->addWidget(m_bgCombo, 1);
    form->addRow(row1);

    // ---- 第 2 行：Text ----
    m_textEdit = new QLineEdit(this);
    m_textEdit->setPlaceholderText(QStringLiteral("匹配的目标内容"));
    form->addRow(QStringLiteral("Text"), m_textEdit);

    // ---- 第 3 行：Description ----
    m_descEdit = new QLineEdit(this);
    m_descEdit->setPlaceholderText(QStringLiteral("规则的备注说明"));
    form->addRow(QStringLiteral("Description"), m_descEdit);

    // ---- 底部：三个复选框 ----
    auto* checks = new QWidget(this);
    auto* hc = new QHBoxLayout(checks);
    hc->setContentsMargins(0, 0, 0, 0);
    m_excludeCheck = new QCheckBox(QStringLiteral("Excluding [!]"), checks);
    m_excludeCheck->setToolTip(QStringLiteral("开启后匹配的行被隐藏而非着色"));
    m_caseCheck = new QCheckBox(QStringLiteral("Case-sensitive [Aa]"), checks);
    m_regexCheck = new QCheckBox(QStringLiteral("Regular expression [R]"), checks);
    hc->addWidget(m_excludeCheck);
    hc->addWidget(m_caseCheck);
    hc->addWidget(m_regexCheck);
    form->addRow(checks);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_textEdit, &QLineEdit::returnPressed, this, &QDialog::accept);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    setMinimumWidth(520);
}

void FilterRuleDialog::populateColorCombo(QComboBox* combo, bool includeNone) {
    const ColorPreset* presets =
        includeNone ? kBackgroundColors : kForegroundColors;
    const int n = static_cast<int>(
        includeNone ? sizeof(kBackgroundColors) / sizeof(ColorPreset)
                    : sizeof(kForegroundColors) / sizeof(ColorPreset));
    for (int i = 0; i < n; ++i) {
        const ColorPreset& c = presets[i];
        QPixmap pm(16, 16);
        if (c.value & 0xFF000000u) {
            pm.fill(QColor(QRgb(c.value)));
        } else {
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setPen(QColor(0xAA, 0xAA, 0xAA));
            p.drawRect(0, 0, 15, 15);
        }
        combo->addItem(QIcon(pm), QString::fromLatin1(c.name), c.value);
    }
}

void FilterRuleDialog::setRule(const FilterRule& r) {
    m_filterCombo->setCurrentIndex(
        m_filterCombo->findData(static_cast<int>(r.matchType)));
    if (m_filterCombo->currentIndex() < 0)
        m_filterCombo->setCurrentIndex(1);  // Contains 兜底
    m_fgCombo->setCurrentIndex(m_fgCombo->findData(r.foreground));
    if (m_fgCombo->currentIndex() < 0) m_fgCombo->setCurrentIndex(1);
    m_bgCombo->setCurrentIndex(m_bgCombo->findData(r.background));
    if (m_bgCombo->currentIndex() < 0) m_bgCombo->setCurrentIndex(3);
    m_textEdit->setText(QString::fromStdString(r.pattern));
    m_descEdit->setText(QString::fromStdString(r.description));
    m_excludeCheck->setChecked(r.action == FilterAction::Exclude);
    m_caseCheck->setChecked(r.caseSensitive);
    m_regexCheck->setChecked(r.mode == MatchMode::Regex);
}

FilterRule FilterRuleDialog::rule() const {
    FilterRule r;
    r.id = m_editId;  // 添加模式为 -1，调用方分配
    r.pattern = m_textEdit->text().toStdString();
    r.description = m_descEdit->text().toStdString();
    r.action = m_excludeCheck->isChecked() ? FilterAction::Exclude
                                           : FilterAction::Include;
    r.mode = m_regexCheck->isChecked() ? MatchMode::Regex
                                       : MatchMode::Substring;
    r.matchType =
        static_cast<FilterMatch>(m_filterCombo->currentData().toInt());
    r.foreground = m_fgCombo->currentData().toUInt();
    r.background = m_bgCombo->currentData().toUInt();
    r.caseSensitive = m_caseCheck->isChecked();
    return r;
}

void FilterRuleDialog::setEditMode(int ruleId) {
    m_editId = ruleId;
    if (ruleId >= 0) {
        setWindowTitle(QStringLiteral("修改过滤规则 #%1").arg(ruleId));
    } else {
        setWindowTitle(QStringLiteral("添加过滤规则"));
    }
}

}  // namespace tat
// =============================================================================
// FindDialog.cpp
// =============================================================================

#include "ui/dialogs/FindDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

namespace tat {

FindDialog::FindDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("查找"));
    auto* form = new QFormLayout(this);

    m_patternEdit = new QLineEdit(this);
    m_patternEdit->setPlaceholderText(QStringLiteral("搜索文本或正则"));
    form->addRow(QStringLiteral("查找:"), m_patternEdit);

    auto* optsRow = new QFormLayout();
    m_caseCheck = new QCheckBox(QStringLiteral("区分大小写"), this);
    m_regexCheck = new QCheckBox(QStringLiteral("正则表达式"), this);
    m_wholeWordCheck = new QCheckBox(QStringLiteral("全词匹配"), this);
    optsRow->addRow(m_caseCheck);
    optsRow->addRow(m_regexCheck);
    optsRow->addRow(m_wholeWordCheck);
    form->addRow(QString(), optsRow);

    m_resultLabel = new QLabel(QStringLiteral("尚未搜索"), this);
    form->addRow(QString(), m_resultLabel);

    // 增量搜索防抖：输入停止 200 ms 后在当前视口 ±500 行内即时计数
    //（MUST 用 QTimer，§7.5.2 同款约束）
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        if (m_patternEdit->text().isEmpty() || m_regexCheck->isChecked())
            return;  // 空模式/正则模式不做视口增量（正则全量走 Enter）
        emit incrementalSearchRequested(options());
    });

    auto* buttons = new QDialogButtonBox(this);
    m_findButton = buttons->addButton(QStringLiteral("查找"),
                                      QDialogButtonBox::ActionRole);
    auto* nextBtn = buttons->addButton(QStringLiteral("下一个 (F3)"),
                                       QDialogButtonBox::ActionRole);
    auto* prevBtn = buttons->addButton(QStringLiteral("上一个 (Shift+F3)"),
                                       QDialogButtonBox::ActionRole);
    buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    form->addRow(buttons);
    connect(nextBtn, &QPushButton::clicked, this,
            [this] { emit nextHitRequested(+1); });
    connect(prevBtn, &QPushButton::clicked, this,
            [this] { emit nextHitRequested(-1); });

    connect(m_patternEdit, &QLineEdit::textChanged, this,
            &FindDialog::onPatternEdited);
    connect(m_findButton, &QPushButton::clicked, this,
            &FindDialog::onFindClicked);
    connect(m_patternEdit, &QLineEdit::returnPressed, this,
            &FindDialog::onFindClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
}

SearchOptions FindDialog::options() const {
    SearchOptions opt;
    opt.pattern = m_patternEdit->text().toStdString();
    opt.caseSensitive = m_caseCheck->isChecked();
    opt.useRegex = m_regexCheck->isChecked();
    opt.wholeWord = m_wholeWordCheck->isChecked();
    return opt;
}

void FindDialog::onPatternEdited(const QString& text) {
    if (text.isEmpty()) {
        m_debounce->stop();
        m_resultLabel->setText(QStringLiteral("尚未搜索"));
        return;
    }
    m_debounce->start();
}

void FindDialog::onIncrementalResult(int viewportCount) {
    m_resultLabel->setText(viewportCount > 0
        ? QStringLiteral("当前视口内 %1 处（Enter 全量搜索）").arg(viewportCount)
        : QStringLiteral("视口内无匹配（Enter 全量搜索）"));
}

void FindDialog::onFindClicked() {
    if (m_patternEdit->text().isEmpty()) return;
    emit searchRequested(options());
}

void FindDialog::onSearchCompleted(const SearchResult& result) {
    QString text;
    if (result.truncated > 0)
        text = QStringLiteral("找到 %1 处（另有 %2 处截断）")
                   .arg(result.hits.size())
                   .arg(result.truncated);
    else
        text = QStringLiteral("找到 %1 处").arg(result.hits.size());
    if (result.hits.empty()) text = QStringLiteral("无匹配");
    m_resultLabel->setText(text);
}

}  // namespace tat
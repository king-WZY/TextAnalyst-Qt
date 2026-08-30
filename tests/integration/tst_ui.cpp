// =============================================================================
// tst_ui.cpp: UI 集成测试（QTest + offscreen）
// 覆盖（R-22 弹窗化交互）：
//   1) 双击日志行 → 弹出"添加过滤规则"对话框并预填该行文本 →
//      关闭后销毁（WA_DeleteOnClose）
//   2) 弹窗确认 → 规则入列表（调用方逻辑）
//   3) FilterRuleDialog setRule/rule 往返（添加/修改双模式）
// 文档：DISPLAYDESIGN.md §7.5.2（R-13/R-14/R-22）
// =============================================================================

#include <QTemporaryDir>
#include <QtTest>

#include "controller/MainController.h"
#include "io/SettingsManager.h"
#include "ui/dialogs/FilterRuleDialog.h"
#include "ui/widgets/FilterListModel.h"
#include "ui/widgets/LogListView.h"
#include "ui/widgets/LogViewModel.h"

using namespace tat;

namespace {

QString writeTempLog(QTemporaryDir& dir, const QString& name,
                     const std::string& content) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(content.data(), static_cast<qint64>(content.size()));
    return path;
}

}  // namespace

class TestUi : public QObject {
    Q_OBJECT

private slots:
    void doubleClickOpensPrefilledDialog();
    void acceptedRuleEntersList();
    void dialogRoundTripBothModes();
    void dialogDestroyedOnClose();
    void enableDisableAllPreservesRules();
};

void TestUi::enableDisableAllPreservesRules() {
    // R-23：Enable/Disable All——只改 isEnabled，规则本身保留
    FilterListModel fmodel;
    FilterRule a;
    a.id = 1;
    a.pattern = "a";
    FilterRule b;
    b.id = 2;
    b.pattern = "b";
    b.isEnabled = false;
    fmodel.addRule(a);
    fmodel.addRule(b);
    QCOMPARE(fmodel.ruleAt(1).isEnabled, false);

    fmodel.enableAll(true);
    QVERIFY(fmodel.ruleAt(0).isEnabled);
    QVERIFY(fmodel.ruleAt(1).isEnabled);
    QCOMPARE(fmodel.rowCount(), 2);  // 未删除

    fmodel.enableAll(false);
    QVERIFY(!fmodel.ruleAt(0).isEnabled);
    QVERIFY(!fmodel.ruleAt(1).isEnabled);
    QCOMPARE(fmodel.rowCount(), 2);
}

void TestUi::doubleClickOpensPrefilledDialog() {
    QTemporaryDir dir;
    const QString path =
        writeTempLog(dir, "ui.log", "first line\nERROR payload here\nthird\n");
    QVERIFY(!path.isEmpty());

    MainController ctl;
    SettingsManager settings;
    ctl.setSettingsManager(&settings);
    QSignalSpy opened(&ctl, &MainController::fileOpened);
    ctl.openFile(path);
    QVERIFY(opened.wait(5000));
    QCOMPARE(ctl.rowCount(), 3);

    LogViewModel model(&ctl);
    LogListView view(&ctl);
    view.setModel(&model);
    view.resize(480, 200);
    view.show();
    QTRY_VERIFY(model.rowCount() == 3);

    // 与 MainWindow 相同的连接语义：双击 → 打开添加弹窗（预填行文本）
    QObject::connect(&view, &LogListView::lineActivated, &view,
                     [&ctl, &view](int lineNo) {
                         auto buffer = ctl.bufferSnapshot();
                         if (!buffer || lineNo < 1 ||
                             lineNo > buffer->rowCount())
                             return;
                         auto* dlg = new FilterRuleDialog(&view);
                         QObject::connect(dlg, &QDialog::finished, dlg,
                                          &QObject::deleteLater);
                         FilterRule init;
                         init.pattern = buffer->toUtf8(lineNo - 1);
                         dlg->setRule(init);
                         dlg->open();  // 窗口模态，非阻塞
                     });
    view.scrollToLine(2);
    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       view.visualRect(model.index(1, 0)).center());

    // 弹窗出现且预填第 2 行文本
    auto* dlg = view.findChild<FilterRuleDialog*>();
    QVERIFY(dlg != nullptr);
    QTRY_VERIFY(dlg->isVisible());
    QCOMPARE(dlg->rule().pattern, std::string("ERROR payload here"));
    QCOMPARE(dlg->editRuleId(), -1);  // 添加模式

    QSignalSpy destroyed(dlg, &QObject::destroyed);
    dlg->reject();  // 取消 → finished → 销毁，不占空间（R-22）
    QVERIFY(destroyed.wait(2000));
    QCOMPARE(view.findChild<FilterRuleDialog*>(), nullptr);
}

void TestUi::acceptedRuleEntersList() {
    MainController ctl;
    SettingsManager settings;
    ctl.setSettingsManager(&settings);
    QSignalSpy applied(&ctl, &MainController::filterApplied);

    // 模拟"添加弹窗确认"的调用方逻辑：accepted → 规则入列表 → apply
    FilterListModel fmodel;
    auto* dlg = new FilterRuleDialog(nullptr);
    QObject::connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    FilterRule init;
    init.pattern = "ERROR";
    dlg->setRule(init);
    QObject::connect(dlg, &QDialog::accepted, &fmodel, [&fmodel, dlg] {
        FilterRule r = dlg->rule();
        r.id = 1;
        fmodel.addRule(r);
    });
    dlg->accept();  // 用户按确定

    QCOMPARE(fmodel.rowCount(), 1);
    QCOMPARE(QString::fromStdString(fmodel.ruleAt(0).pattern),
             QString("ERROR"));
    QCOMPARE(QString::fromStdString(fmodel.ruleAt(0).description),
             QString(""));  // 空备注正常往返

    // 规则集可被引擎应用（着色/排除由复选框决定，此处默认着色）
    auto rules = fmodel.rules();
    std::shared_ptr<RuleSet> rs;
    const Error e = FilterEngine::buildRuleSet(
        {rules.first()}, 0,
        [](const std::string& p, bool, bool, std::string* o,
           std::string*) -> int {
            *o = p;
            return 0;
        },
        &rs);
    QVERIFY(e.ok());
    QVERIFY(rs->anyRule);
}

void TestUi::dialogRoundTripBothModes() {
    FilterRuleDialog dlg;
    FilterRule in;
    in.id = 7;
    in.pattern = "WARN|FATAL";
    in.description = "warn rule";
    in.action = FilterAction::Exclude;
    in.mode = MatchMode::Regex;
    in.matchType = FilterMatch::StartsWith;
    in.foreground = 0xFF5588FF;
    in.background = 0xFF88AAFF;
    in.caseSensitive = true;

    // 修改模式：setEditMode → rule().id 回带
    dlg.setEditMode(in.id);
    dlg.setRule(in);
    QCOMPARE(dlg.editRuleId(), 7);
    const FilterRule out = dlg.rule();
    QCOMPARE(out.id, 7);
    QCOMPARE(QString::fromStdString(out.pattern), QString("WARN|FATAL"));
    QCOMPARE(QString::fromStdString(out.description), QString("warn rule"));
    QCOMPARE((int)out.action, (int)FilterAction::Exclude);
    QCOMPARE((int)out.mode, (int)MatchMode::Regex);
    QCOMPARE((int)out.matchType, (int)FilterMatch::StartsWith);
    QCOMPARE(out.foreground, 0xFF5588FFu);
    QCOMPARE(out.background, 0xFF88AAFFu);
    QVERIFY(out.caseSensitive);

    // 添加模式：不 setEditMode → id 为 -1
    FilterRuleDialog addDlg;
    addDlg.setRule(FilterRule{.pattern = "x"});
    QCOMPARE(addDlg.editRuleId(), -1);
    QCOMPARE(addDlg.rule().id, -1);
}

void TestUi::dialogDestroyedOnClose() {
    FilterRuleDialog* dlg = new FilterRuleDialog(nullptr);
    QObject::connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    QSignalSpy destroyed(dlg, &QObject::destroyed);
    dlg->open();
    dlg->reject();  // 取消 → finished → deleteLater（R-22 销毁协议）
    QVERIFY(destroyed.wait(2000));
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
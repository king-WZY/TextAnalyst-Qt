// =============================================================================
// tst_io.cpp: IO 层集成测试（QtTest）
// 覆盖：TatSerializer 往返/备份/异常文件；SettingsManager 会话；
//       FileWatcher inotify 事件（事件循环 + QSignalSpy）
// =============================================================================

#include <QTemporaryDir>
#include <QtTest>

#include "core/models/FilterRule.h"
#include "io/FileWatcher.h"
#include "io/LogRotator.h"
#include "io/SettingsManager.h"
#include "io/TatSerializer.h"

using namespace tat;

class TestIo : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);  // 配置隔离到 ~/.qttest
    }

    void writeReadRoundtrip() {
        QTemporaryDir dir;
        const QString path = dir.filePath("rules.tat");
        std::vector<FilterRule> rules;
        FilterRule r;
        r.id = 1;
        r.action = FilterAction::Exclude;
        r.pattern = "ERROR";
        r.foreground = 0xFF112233;
        r.background = 0xFF445566;
        r.caseSensitive = true;
        r.wholeWord = true;
        r.rank = 2;
        rules.push_back(r);
        FilterRule r2;
        r2.id = 2;
        r2.action = FilterAction::Include;
        r2.mode = MatchMode::Regex;
        r2.pattern = "WARN|FATAL";
        r2.matchType = FilterMatch::StartsWith;   // 扩展属性：匹配锚定
        r2.description = "warn only rule";        // 扩展属性：备注
        rules.push_back(r2);

        QVERIFY(TatSerializer::write(path, rules).ok());

        std::vector<FilterRule> out;
        QVERIFY(TatSerializer::read(path, &out).ok());
        QCOMPARE((int)out.size(), 2);
        QCOMPARE(QString::fromStdString(out[0].pattern), QString("ERROR"));
        QCOMPARE(out[0].foreground, 0xFF112233u);
        QCOMPARE(out[0].background, 0xFF445566u);
        QVERIFY(out[0].caseSensitive);
        QVERIFY(out[0].wholeWord);
        QCOMPARE(out[0].id, 1);
        QCOMPARE(out[0].rank, 2);
        QCOMPARE((int)out[1].mode, (int)MatchMode::Regex);
        QCOMPARE((int)out[1].action, (int)FilterAction::Include);
        QCOMPARE((int)out[1].matchType, (int)FilterMatch::StartsWith);
        QCOMPARE(QString::fromStdString(out[1].description),
                 QString("warn only rule"));
        QCOMPARE((int)out[0].matchType, (int)FilterMatch::Contains);  // 缺省值
    }

    void readMissingFile() {
        std::vector<FilterRule> out;
        const auto e = TatSerializer::read("/nonexistent/definitely.tat", &out);
        QVERIFY(e.ok());           // 无文件 → 空规则集（合法）
        QCOMPARE((int)out.size(), 0);
    }

    void readMalformedXml() {
        QTemporaryDir dir;
        const QString path = dir.filePath("bad.tat");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not xml <<<");
        f.close();
        std::vector<FilterRule> out;
        const auto e = TatSerializer::read(path, &out);
        QCOMPARE((int)e.code, (int)ErrCode::XmlError);
    }

    void backupBehavior() {
        QTemporaryDir dir;
        const QString path = dir.filePath("rules.tat");
        std::vector<FilterRule> v1{FilterRule{.id = 1,
                                              .action = FilterAction::Exclude,
                                              .pattern = "OLD"}};
        std::vector<FilterRule> v2{FilterRule{.id = 1,
                                              .action = FilterAction::Exclude,
                                              .pattern = "NEW"}};
        QVERIFY(TatSerializer::write(path, v1).ok());
        QVERIFY(TatSerializer::write(path, v2).ok());
        // 第二次写入后：主文件 = v2，备份 = v1
        std::vector<FilterRule> cur, bak;
        QVERIFY(TatSerializer::read(path, &cur).ok());
        QVERIFY(TatSerializer::read(path + ".bak", &bak).ok());
        QCOMPARE(QString::fromStdString(cur[0].pattern), QString("NEW"));
        QCOMPARE(QString::fromStdString(bak[0].pattern), QString("OLD"));
        // 无 .tmp 残留（原子替换）
        QVERIFY(!QFile::exists(path + ".tmp"));
    }

    void settingsMarkersRoundtrip() {
        SettingsManager s;
        std::vector<Marker> in{{12, 2}, {34, 5}, {56, 8}};
        s.setSessionMarkers(in);
        const auto out = s.sessionMarkers();
        QCOMPARE((int)out.size(), 3);
        QCOMPARE(out[0].line, 12);
        QCOMPARE(out[0].markerId, 2);
        s.setTabWidth(8);
        QCOMPARE(s.tabWidth(), 8);
        s.setDebounceMs(350);
        QCOMPARE(s.debounceMs(), 350);
        s.setRecentFiles({});
        s.addRecentFile("a");
        s.addRecentFile("a");           // 去重（addRecentFile 语义）
        s.addRecentFile("b");
        QCOMPARE(s.recentFiles().size(), 2);
        QCOMPARE(s.recentFiles().first(), QString("b"));  // 最近的在最前
    }

    void logRotatorRollsOver() {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        {
            LogRotator rot(path, /*maxSize=*/64, /*maxBackups=*/2);
            for (int i = 0; i < 20; ++i) rot.append(QString("line %1").arg(i));
        }  // 析构落盘
        QVERIFY(QFile::exists(path));            // 当前档
        QVERIFY(QFile::exists(path + ".1"));     // 第一历史档
        QVERIFY(QFile::exists(path + ".2"));     // 第二历史档
        QVERIFY(!QFile::exists(path + ".3"));    // 超出保留数即被删除
        qint64 total = 0;
        for (const QString& p : {path, path + ".1", path + ".2"})
            total += QFileInfo(p).size();
        // 每档 ≤ maxSize（写入前判断，允许最后一行刚好处在边界内）
        QVERIFY(QFileInfo(path).size() <= 64);
        QVERIFY(QFileInfo(path + ".1").size() <= 64);
        QVERIFY(QFileInfo(path + ".2").size() <= 64);
        QVERIFY(total > 0);
    }

    void fileWatcherSignals() {
        QTemporaryDir dir;
        const QString path = dir.filePath("watch.log");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("line one\n");
        }
        FileWatcher w;
        QSignalSpy spy(&w, &FileWatcher::externalChanged);
        QVERIFY(w.watch(path));
        QVERIFY(w.isWatching());

        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::Append));
            f.write("line two\n");
            f.flush();
        }
        QVERIFY(spy.wait(3000));  // inotify 事件 → 信号
        QVERIFY(spy.count() >= 1);
    }
};

QTEST_GUILESS_MAIN(TestIo)
#include "tst_io.moc"
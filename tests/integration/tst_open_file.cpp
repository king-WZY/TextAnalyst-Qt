// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// tst_open_file.cpp: 集成测试（真实 mmap + MainController 异步流程）
// 文档：DISPLAYDESIGN.md §10.3
// =============================================================================

#include <QTemporaryDir>
#include <QtTest>

#include "buffer/EncodingDetector.h"
#include "buffer/TextBuffer.h"
#include "controller/MainController.h"
#include "io/SettingsManager.h"

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

std::string bigLog(size_t miB) {
    std::string out;
    out.reserve(miB * (1u << 20));
    int i = 0;
    while (out.size() < miB * (1u << 20)) {
        out += "2026-08-29 12:00:00.000 INFO worker task=";
        out += std::to_string(i);
        out += " processing payload number ";
        out += std::to_string(i * 7);
        out += "\n";
        ++i;
    }
    return out;
}

}  // namespace

class TestOpenFile : public QObject {
    Q_OBJECT

private slots:
    void openSmallFile();
    void openLargeFile();
    void openGbkFile();
    void openEmptyFile();
    void openMissingFile();
    void controllerOpenFlow();
    void controllerFilterFlow();
};

void TestOpenFile::openSmallFile() {
    QTemporaryDir dir;
    const QString path = writeTempLog(dir, "small.log", "a\nb\nc\n");
    QVERIFY(!path.isEmpty());
    Error err;
    EncodingInfo enc;
    auto tb = TextBuffer::create(path.toStdString(), &err, &enc, nullptr, nullptr);
    QVERIFY(err.ok());
    QVERIFY(tb != nullptr);
    QCOMPARE(tb->rowCount(), 3);
    QCOMPARE(QString::fromStdString(std::string(tb->textAt(1))), QString("b"));
    QVERIFY(tb->totalBytes() >= 6);
}

void TestOpenFile::openLargeFile() {
    // 50 MiB 日志：索引应显著快于两秒（宽松门禁，机器性能兜底）
    QTemporaryDir dir;
    const std::string log = bigLog(50);
    const QString path = writeTempLog(dir, "large.log", log);
    QVERIFY(!path.isEmpty());
    QElapsedTimer t;
    t.start();
    Error err;
    auto tb = TextBuffer::create(path.toStdString(), &err, nullptr, nullptr,
                                 nullptr);
    const qint64 ms = t.elapsed();
    QVERIFY(err.ok());
    QVERIFY(tb != nullptr);
    QVERIFY(tb->rowCount() > 10000);
    QVERIFY2(ms < 5000, qPrintable(QString("index took %1 ms").arg(ms)));
}

void TestOpenFile::openGbkFile() {
    // "你好" GBK 编码 + 换行（刻意取必含非法 UTF-8 首字节的样本，见 tst_buffer）
    const unsigned char kGbk[] = {0xC4, 0xE3, 0xBA, 0xC3, 0x0A, 0x41, 0x0A};
    QTemporaryDir dir;
    const QString path = dir.filePath("gbk.log");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(reinterpret_cast<const char*>(kGbk),
                static_cast<qint64>(sizeof(kGbk)));
    }
    Error err;
    EncodingInfo enc;
    auto tb = TextBuffer::create(path.toStdString(), &err, &enc, nullptr, nullptr);
    QVERIFY(err.ok());
    // 完整 detect 应识别为 GB18030 家族
    auto full = EncodingDetector::detect(reinterpret_cast<const char*>(kGbk),
                                         6);
    QVERIFY(full.encoding == Encoding::GB18030 ||
            full.encoding == Encoding::Gbk);
}

void TestOpenFile::openEmptyFile() {
    QTemporaryDir dir;
    const QString path = writeTempLog(dir, "empty.log", "");
    QVERIFY(!path.isEmpty());
    Error err;
    auto tb = TextBuffer::create(path.toStdString(), &err, nullptr, nullptr,
                                 nullptr);
    QVERIFY(err.ok());
    QVERIFY(tb->isValid());
    QCOMPARE(tb->rowCount(), 0);  // R-06 空文件语义
}

void TestOpenFile::openMissingFile() {
    Error err;
    auto tb = TextBuffer::create("/nonexistent/missing.log", &err, nullptr,
                                 nullptr, nullptr);
    QVERIFY(tb == nullptr);
    QVERIFY(err.code == ErrCode::FileNotFound ||
            err.code == ErrCode::PermissionDenied);
}

void TestOpenFile::controllerOpenFlow() {
    QTemporaryDir dir;
    const QString path = writeTempLog(dir, "c.log", "aaa\nbbb\nccc\n");
    QVERIFY(!path.isEmpty());

    MainController ctl;
    auto settings = std::make_unique<SettingsManager>();
    ctl.setSettingsManager(settings.get());

    QSignalSpy opened(&ctl, &MainController::fileOpened);
    QSignalSpy failed(&ctl, &MainController::fileOpenFailed);
    ctl.openFile(path);
    QVERIFY(opened.wait(5000));  // 后台负载 → fileOpened
    QCOMPARE(opened.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(ctl.rowCount(), 3);
    QVERIFY(ctl.bufferSnapshot() != nullptr);
}

void TestOpenFile::controllerFilterFlow() {
    QTemporaryDir dir;
    const QString path =
        writeTempLog(dir, "f.log", "ERROR a\nINFO b\nERROR c\nINFO d\n");
    QVERIFY(!path.isEmpty());

    MainController ctl;
    auto settings = std::make_unique<SettingsManager>();
    ctl.setSettingsManager(settings.get());

    QSignalSpy opened(&ctl, &MainController::fileOpened);
    ctl.openFile(path);
    QVERIFY(opened.wait(5000));

    std::vector<FilterRule> rules;
    FilterRule r;
    r.id = 1;
    r.action = FilterAction::Exclude;
    r.pattern = "ERROR";
    rules.push_back(r);

    QSignalSpy applied(&ctl, &MainController::filterApplied);
    ctl.applyFilter(rules);
    QVERIFY(applied.wait(5000));
    QCOMPARE(applied.count(), 1);
    QVERIFY(applied.takeFirst().at(0).toBool());  // ok == true

    auto result = ctl.resultSnapshot();
    QVERIFY(result != nullptr);
    QCOMPARE((int)result->states.size(), 4);
    RowState s0;
    s0.raw = result->states[0];
    QCOMPARE((int)s0.state(), (int)ResultState::Hidden);   // ERROR a
    RowState s1;
    s1.raw = result->states[1];
    QCOMPARE((int)s1.state(), (int)ResultState::Normal);   // INFO b
    QCOMPARE(result->matchCounts[1].load(), 2);            // 两条 ERROR
}

QTEST_MAIN(TestOpenFile)
#include "tst_open_file.moc"
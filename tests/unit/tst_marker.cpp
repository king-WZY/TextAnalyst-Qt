// =============================================================================
// tst_marker.cpp: 标记管理单元测试（DISPLAYDESIGN.md §10.2.3）
// =============================================================================

#include "engine/MarkerManager.h"
#include "minitest.h"

using namespace tat;

TEST(addRemove) {
    MarkerManager m;
    CHECK(m.add(5, 1));
    CHECK(m.has(5, 1));
    CHECK(m.remove(5, 1));
    CHECK(!m.has(5, 1));
    CHECK_EQ(m.totalCount(), 0);
}

TEST(toggle) {
    MarkerManager m;
    CHECK(m.toggle(8, 2));   // 添加
    CHECK(m.has(8, 2));
    CHECK(!m.toggle(8, 2));  // 移除
    CHECK(!m.has(8, 2));
}

TEST(nextForward) {
    MarkerManager m;
    m.add(3, 1); m.add(10, 1); m.add(20, 1);
    auto r = m.next(3, 1, true);
    CHECK(r.has_value() && *r == 10);
}

TEST(nextBackward) {
    MarkerManager m;
    m.add(3, 1); m.add(10, 1); m.add(20, 1);
    auto r = m.next(10, 1, false);
    CHECK(r.has_value() && *r == 3);
}

TEST(wrapForward) {
    MarkerManager m;
    m.add(3, 1); m.add(10, 1);
    auto r = m.next(10, 1, true);  // 末尾环绕到开头
    CHECK(r.has_value() && *r == 3);
}

TEST(wrapBackward) {
    MarkerManager m;
    m.add(3, 1); m.add(10, 1);
    auto r = m.next(3, 1, false);  // 开头环绕到末尾
    CHECK(r.has_value() && *r == 10);
}

TEST(multipleMarkersIndependent) {
    MarkerManager m;
    m.add(5, 1);
    m.add(5, 2);   // 同一行可以有两个标记（不同类型互不冲突）
    CHECK(m.has(5, 1) && m.has(5, 2));
    CHECK_EQ(m.markerOf(5), 1);   // 类型升序第一个
    CHECK_EQ(m.count(1), 1);
    CHECK_EQ(m.count(2), 1);
    CHECK_EQ(m.totalCount(), 2);
}

TEST(duplicateAdd) {
    MarkerManager m;
    CHECK(m.add(7, 3));
    CHECK(!m.add(7, 3));  // 重复添加失败
    CHECK_EQ(m.count(3), 1);
}

TEST(removeNonexistent) {
    MarkerManager m;
    CHECK(!m.remove(99, 1));
    CHECK(!m.remove(1, 0));    // 非法 markerId
    CHECK(!m.remove(1, 9));
    CHECK(!m.add(0, 1));       // 非法行号
}

TEST(clearAll) {
    MarkerManager m;
    m.add(1, 1); m.add(2, 5); m.add(3, 8);
    m.clear();
    CHECK_EQ(m.totalCount(), 0);
}

TEST(saveLoad) {
    MarkerManager m;
    m.add(12, 2); m.add(34, 5); m.add(56, 8);
    std::vector<Marker> saved;
    m.save(&saved);
    CHECK_EQ(saved.size(), 3u);
    MarkerManager m2;
    m2.load(saved);
    CHECK(m2.has(12, 2) && m2.has(34, 5) && m2.has(56, 8));
    CHECK_EQ(m2.totalCount(), 3);
}

TEST(sortedInvariant) {
    // 乱序插入后 dump 应有序（跳转语义依赖）
    MarkerManager m;
    for (int i = 100; i >= 1; --i) m.add(i * 7, 4);
    auto v = m.dump();
    const auto& a = v[3];
    CHECK_EQ(a.size(), 100u);
    for (size_t i = 1; i < a.size(); ++i) CHECK(a[i - 1] < a[i]);
}

TEST(performance) {
    // 10 万标记：升序批量添加 + 10 万次查找 + 跳转，目标 < 2 s（1 亿行场景外推）
    MarkerManager m;
    const int kN = 100000;
    for (int i = 1; i <= kN; ++i) m.add(i, 1);
    int hits = 0;
    for (int i = 0; i < kN; ++i) {
        if (m.has((i * 7919) % kN + 1, 1)) ++hits;
    }
    CHECK_EQ(hits, kN);  // 模 7919 与 N 互质 → 全命中
    auto r = m.next(kN / 2, 1, true);
    CHECK(r.has_value());
}

int main() { return minitest::runAll(); }
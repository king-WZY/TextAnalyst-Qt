// =============================================================================
// Marker.h: 行标记值的简单载体
// 文档：DISPLAYDESIGN.md §1.7；容器与算法见 engine/MarkerManager.h（§3.4）
// =============================================================================
#pragma once

#include <cstdint>

#include "models/common.h"

namespace tat {

// markerId: 1..8（1-based，对应 Ctrl+1..8 / Alt+1..8）
struct Marker {
    int line = 0;
    int markerId = 0;
};

}  // namespace tat
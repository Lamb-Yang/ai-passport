// main/camping_model.h —— 露营达人(帐篷谜题)纯逻辑模型。
// 不依赖 ESP-IDF/LVGL,可在宿主机测试。规则与原网页版一致:
//   1. 每枚帐篷至少有一格与树正交相邻(斜角不算);
//   2. 不同帐篷八方向都不能接触(双帐篷自身两格除外);
//   3. 行/列提示数字 = 该行/列帐篷占用的格数;
//   4. 库存单/双帐篷全部放下且行列数字全部吻合且无警告 → 过关。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CAMPING_SIZE       6
#define CAMPING_CELL_COUNT (CAMPING_SIZE * CAMPING_SIZE)
#define CAMPING_MAX_TREES   8
#define CAMPING_MAX_PIECES 16   // 单人最多 8 枚 + 双人最多 4 枚,留余量

// 帐篷类型
typedef enum {
    CAMPING_TENT_SINGLE = 0,   // 占 1 格
    CAMPING_TENT_DOUBLE,       // 占 2 格,横放或竖放
} camping_tent_kind_t;

// 双帐篷朝向(锚点格为左格/上格)
typedef enum {
    CAMPING_ORIENT_H = 0,
    CAMPING_ORIENT_V,
} camping_orient_t;

// 关卡数据(树位、行列提示、帐篷数量)。指针版由 levels.h 提供具体表。
typedef struct {
    uint8_t tree_count;
    uint8_t trees[CAMPING_MAX_TREES];   // 格索引,0..35
    uint8_t row_clues[CAMPING_SIZE];
    uint8_t col_clues[CAMPING_SIZE];
    uint8_t singles;                    // 需要的单人帐篷数
    uint8_t doubles;                    // 需要的双人帐篷数
} camping_level_t;

// 已放置的一枚帐篷
typedef struct {
    uint8_t kind;          // camping_tent_kind_t
    uint8_t cell_count;    // 1 或 2
    uint8_t cells[2];      // [1] 仅双人使用
} camping_piece_t;

// 棋盘(放置状态)
typedef struct {
    camping_piece_t pieces[CAMPING_MAX_PIECES];
    uint8_t piece_count;
} camping_board_t;

// 一次局面的分析结果
typedef struct {
    uint8_t rows[CAMPING_SIZE];       // 每行帐篷格数
    uint8_t cols[CAMPING_SIZE];       // 每列帐篷格数
    uint8_t used_singles;
    uint8_t used_doubles;
    uint16_t warnings;                // bit i = pieces[i] 有警告
    bool inventory_complete;
    bool clues_complete;
    bool valid;                       // 过关
} camping_analysis_t;

static inline uint8_t camping_row_of(uint8_t cell) { return cell / CAMPING_SIZE; }
static inline uint8_t camping_col_of(uint8_t cell) { return cell % CAMPING_SIZE; }
static inline uint8_t camping_cell_at(uint8_t row, uint8_t col)
{
    return row * CAMPING_SIZE + col;
}

static inline bool camping_level_is_tree(const camping_level_t *level, uint8_t cell)
{
    for (uint8_t i = 0; i < level->tree_count; i++) {
        if (level->trees[i] == cell) return true;
    }
    return false;
}

void camping_board_reset(camping_board_t *board);

// 计算帐篷占用的格子。越界返回 false。
bool camping_cells_for(uint8_t kind, uint8_t anchor, uint8_t orient,
                       uint8_t out_cells[2], uint8_t *out_count);

// 判断能否放置:不压树、不与现有帐篷重叠(忽略 ignore_index 对应的帐篷)。
bool camping_can_place(const camping_board_t *board, const camping_level_t *level,
                       uint8_t kind, uint8_t anchor, uint8_t orient,
                       int8_t ignore_index);

// 放置一枚新帐篷。位置非法或库存已用完返回 false。
bool camping_place(camping_board_t *board, const camping_level_t *level,
                   uint8_t kind, uint8_t anchor, uint8_t orient);

// 删除一枚帐篷。
void camping_remove(camping_board_t *board, uint8_t piece_index);

// 返回占用该格的帐篷下标,无则 -1。
int8_t camping_piece_at(const camping_board_t *board, uint8_t cell);

camping_analysis_t camping_analyze(const camping_board_t *board,
                                   const camping_level_t *level);

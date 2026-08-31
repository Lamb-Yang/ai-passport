// tests/test_camping_model.c —— 露营达人逻辑的宿主机测试。
// 核心验证:全部 600 关的官方答案都符合过关规则(判胜逻辑没有遗漏任何一关);
// 以及单点规则:压树/重叠/库存/接触/无支撑等场景都被正确拦截。
#include <assert.h>
#include <stdio.h>

#include "camping_levels.h"
#include "camping_model.h"

// 答案夹具(由 tools/gen_camping_levels.py 生成,不参与固件编译)
#include "camping_answer.h"

static const camping_answer_t *answer_table(camping_diff_t diff)
{
    static const camping_answer_t *tables[CAMPING_DIFF_COUNT] = {
        CAMPING_ANSWERS_EASY, CAMPING_ANSWERS_MEDIUM, CAMPING_ANSWERS_HARD,
    };
    return tables[diff];
}

// 每关:按官方答案放置,必须判胜且无任何警告。
static void test_all_official_answers_win(void)
{
    camping_board_t board;
    int checked = 0;
    for (camping_diff_t diff = 0; diff < CAMPING_DIFF_COUNT; diff++) {
        const camping_level_t *levels = camping_levels_of(diff);
        const camping_answer_t *answers = answer_table(diff);
        for (int i = 0; i < CAMPING_LEVELS_PER_DIFFICULTY; i++) {
            camping_board_reset(&board);
            for (uint8_t s = 0; s < answers[i].single_count; s++) {
                bool ok = camping_place(&board, &levels[i], CAMPING_TENT_SINGLE,
                                        answers[i].single_cells[s], CAMPING_ORIENT_H);
                assert(ok);
            }
            for (uint8_t d = 0; d < answers[i].double_count; d++) {
                uint8_t a = answers[i].double_cells[d][0];
                uint8_t b = answers[i].double_cells[d][1];
                uint8_t orient = (b - a == CAMPING_SIZE) ? CAMPING_ORIENT_V : CAMPING_ORIENT_H;
                bool ok = camping_place(&board, &levels[i], CAMPING_TENT_DOUBLE, a, orient);
                assert(ok);
            }
            camping_analysis_t res = camping_analyze(&board, &levels[i]);
            if (!res.valid) {
                fprintf(stderr, "level %d[%d] official answer rejected\n", diff, i);
            }
            assert(res.valid);
            assert(res.warnings == 0);
            assert(res.used_singles == levels[i].singles);
            assert(res.used_doubles == levels[i].doubles);
            checked++;
        }
    }
    printf("  全部 %d 关官方答案判胜: PASS\n", checked);
}

static const camping_level_t *easy_level(int i)
{
    return &CAMPING_LEVELS_EASY[i];
}

static void test_placement_rules(void)
{
    // easy[0]: 树 {2,6,8,24,25,27,34}, 答案单人帐篷 {1,3,14,28}
    const camping_level_t *lv = easy_level(0);
    camping_board_t board;
    camping_board_reset(&board);

    // 压在树上不能放
    assert(!camping_place(&board, lv, CAMPING_TENT_SINGLE, 2, CAMPING_ORIENT_H));
    // 越界:最右列放横双人
    assert(!camping_place(&board, lv, CAMPING_TENT_DOUBLE, 5, CAMPING_ORIENT_H));
    // 越界:最后一行放竖双人
    assert(!camping_place(&board, lv, CAMPING_TENT_DOUBLE, 30, CAMPING_ORIENT_V));
    // 正常放置
    assert(camping_place(&board, lv, CAMPING_TENT_SINGLE, 1, CAMPING_ORIENT_H));
    // 同格重叠不能放
    assert(!camping_place(&board, lv, CAMPING_TENT_SINGLE, 1, CAMPING_ORIENT_H));
    // 库存超限(该关 4 枚单人,再放 3 枚用完后第 5 枚必须被拒)
    assert(camping_place(&board, lv, CAMPING_TENT_SINGLE, 3, CAMPING_ORIENT_H));
    assert(camping_place(&board, lv, CAMPING_TENT_SINGLE, 14, CAMPING_ORIENT_H));
    assert(camping_place(&board, lv, CAMPING_TENT_SINGLE, 28, CAMPING_ORIENT_H));
    assert(!camping_place(&board, lv, CAMPING_TENT_SINGLE, 16, CAMPING_ORIENT_H));
    // 删除后可再放
    assert(camping_piece_at(&board, 3) == 1);
    camping_remove(&board, 1);
    assert(camping_piece_at(&board, 3) == -1);
    assert(camping_piece_at(&board, 1) == 0);
    printf("  放置规则(压树/越界/重叠/库存/删除): PASS\n");
}

static void test_analysis_warnings(void)
{
    const camping_level_t *lv = easy_level(0);
    camping_board_t board;
    camping_board_reset(&board);

    // 28 号格与树 34 正交相邻(支撑);换成 26 号格(四邻无树 25? 25 是树,26 与 25 相邻=有支撑)
    // 用 13 号格:邻居 7,19,12,14 都不是树 → 无支撑警告
    assert(camping_place(&board, lv, CAMPING_TENT_SINGLE, 13, CAMPING_ORIENT_H));
    camping_analysis_t res = camping_analyze(&board, lv);
    assert(res.warnings != 0);
    assert(!res.valid);
    camping_board_reset(&board);

    // 两枚帐篷八方向接触 → 警告
    assert(camping_place(&board, lv, CAMPING_TENT_SINGLE, 1, CAMPING_ORIENT_H));
    assert(camping_place(&board, lv, CAMPING_TENT_SINGLE, 7, CAMPING_ORIENT_H));   // 1 的右下角
    res = camping_analyze(&board, lv);
    assert(res.warnings != 0);
    assert(!res.valid);
    camping_board_reset(&board);

    // 双帐篷自身两格不算"接触";占位也要对(easy[0] 无双人帐篷,改用 hard 最后一关)
    camping_board_reset(&board);
    const camping_level_t *hd = &CAMPING_LEVELS_HARD[CAMPING_LEVELS_PER_DIFFICULTY - 1];
    assert(camping_place(&board, hd, CAMPING_TENT_DOUBLE, 16, CAMPING_ORIENT_V));   // 占 16+22 两格
    res = camping_analyze(&board, lv);
    assert(res.used_doubles == 1);
    assert(camping_piece_at(&board, 16) == 0 && camping_piece_at(&board, 22) == 0);
    printf("  警告规则(无支撑/帐篷接触/双帐篷自洽): PASS\n");
}

static void test_level_tables(void)
{
    // 表结构一致性:数量与线索范围
    for (camping_diff_t diff = 0; diff < CAMPING_DIFF_COUNT; diff++) {
        const camping_level_t *levels = camping_levels_of(diff);
        for (int i = 0; i < CAMPING_LEVELS_PER_DIFFICULTY; i++) {
            assert(levels[i].tree_count > 0);
            for (uint8_t t = 0; t < levels[i].tree_count; t++) {
                assert(levels[i].trees[t] < CAMPING_CELL_COUNT);
            }
            uint8_t tents = levels[i].singles + 2 * levels[i].doubles;
            uint8_t clue_rows = 0, clue_cols = 0;
            for (int k = 0; k < CAMPING_SIZE; k++) {
                assert(levels[i].row_clues[k] <= CAMPING_SIZE);
                assert(levels[i].col_clues[k] <= CAMPING_SIZE);
                clue_rows += levels[i].row_clues[k];
                clue_cols += levels[i].col_clues[k];
            }
            // 行列提示总数必须等于帐篷总格数,否则关卡无解
            assert(clue_rows == tents);
            assert(clue_cols == tents);
        }
    }
    printf("  关卡表一致性(600 关): PASS\n");
}

int main(void)
{
    test_all_official_answers_win();
    test_placement_rules();
    test_analysis_warnings();
    test_level_tables();
    printf("Host tests (camping): PASS\n");
    return 0;
}

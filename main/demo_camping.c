// main/demo_camping.c —— 露营达人(帐篷谜题)游戏页。
// 移植自原网页版 camping-master.html,规则逻辑在 camping_model.c(纯逻辑,可宿主测试)。
//
// 三键操作(游戏页):
//   上/下 短按  光标在列内上/下移动(环绕)
//   确定 短按   光标右移一列(到最右回第 0 列)
//   确定 双击   放下当前选中帐篷 / 拾起光标处帐篷(拾起后跟随光标)
//   上/下 双击   切换帐篷类型:单人 → 双人横 → 双人竖
//   上  长按    清空棋盘重新摆
//   下  长按    返回关卡选择
//   确定 长按   返回主菜单(main.c 统一拦截)
#include "demo.h"
#include "bsp_display.h"
#include "camping_levels.h"
#include "camping_model.h"
#include "camping_save.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "demo_camping";

// 配色沿用原作"松林/湖泊/珊瑚"主题
#define C_PAPER   0xF7F5EC
#define C_PINE    0x276052
#define C_PINE_D  0x173F37
#define C_LAKE    0x75B9B0
#define C_CORAL   0xE66B55
#define C_SUN     0xF2C85B
#define C_INK     0x263630
#define C_RED     0xE43B2F
#define C_WHITE   0xFFFEF9

#define CELL      28
#define BOARD_X   36
#define BOARD_Y   78

static const char *const DIFF_NAME[CAMPING_DIFF_COUNT] = { "EASY", "MEDIUM", "HARD" };

static void game_start(void);   // 页面间跳转的前向声明
static void page_level_build(void);

typedef enum {
    PG_DIFFICULTY,
    PG_LEVEL,
    PG_GAME,
    PG_CLEAR,
} page_t;

// ---------- 全局状态 ----------
static lv_obj_t *s_scr;
static lv_obj_t *s_page;                  // 当前页容器,切页时整体删除重建
static lv_timer_t *s_timer;               // 游戏页计时器(跑在 LVGL 任务,无需加锁)
static page_t s_page_id;

static camping_diff_t s_diff;
static int s_level_index;                 // 0-based

static camping_board_t s_board;
static camping_analysis_t s_analysis;

// 游戏页 UI 对象
static lv_obj_t *s_cells[CAMPING_CELL_COUNT];
static lv_obj_t *s_decos[CAMPING_CELL_COUNT];
static lv_obj_t *s_row_clues[CAMPING_SIZE];
static lv_obj_t *s_col_clues[CAMPING_SIZE];
static lv_obj_t *s_clock;
static lv_obj_t *s_level_tag;
static lv_obj_t *s_inv_labels[2];
static lv_obj_t *s_help;

// 光标与选择
static uint8_t s_cur_row, s_cur_col;
static uint8_t s_sel_kind;                // camping_tent_kind_t
static uint8_t s_sel_orient;              // camping_orient_t
static bool s_invalid;                    // 最近一次放置失败的光标红框

static int64_t s_start_us;                // 本关开始时间

// ---------- 小工具 ----------
static const camping_level_t *current_level(void)
{
    return &camping_levels_of(s_diff)[s_level_index];
}

static uint32_t elapsed_seconds(void)
{
    return (uint32_t)((esp_timer_get_time() - s_start_us) / 1000000);
}

static void stop_timer(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
}

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

// 切页:删掉旧页容器,清空页内对象指针
static void switch_page(page_t next)
{
    stop_timer();
    s_page_id = next;
    if (s_page) { lv_obj_delete(s_page); s_page = NULL; }
    memset(s_cells, 0, sizeof(s_cells));
    memset(s_decos, 0, sizeof(s_decos));
    memset(s_row_clues, 0, sizeof(s_row_clues));
    memset(s_col_clues, 0, sizeof(s_col_clues));
    s_clock = s_level_tag = s_help = NULL;
    s_inv_labels[0] = s_inv_labels[1] = NULL;
    s_page = block(s_scr, 0, 0, 240, 320, C_PINE_D);   // 全屏容器,内部坐标即屏幕坐标
}

static void format_mmss(uint32_t seconds, char *buf, size_t len)
{
    lv_snprintf(buf, len, "%02u:%02u", seconds / 60, seconds % 60);
}

// ---------- 难度选择页 ----------
static void page_difficulty_build(void)
{
    switch_page(PG_DIFFICULTY);

    for (int d = 0; d < CAMPING_DIFF_COUNT; d++) {
        lv_obj_t *row = block(s_page, 20, 12 + d * 66, 200, 52, C_PAPER);
        lv_obj_set_style_border_width(row, (d == s_diff) ? 3 : 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(d == s_diff ? C_SUN : C_INK), 0);
        lv_obj_t *name = label(row, &lv_font_montserrat_20, C_PINE);
        lv_obj_set_pos(name, 10, 12);
        lv_label_set_text(name, DIFF_NAME[d]);
        lv_obj_t *prog = label(row, &lv_font_montserrat_14, C_PINE);
        lv_obj_set_pos(prog, 108, 17);
        lv_label_set_text_fmt(prog, "%d/200", camping_save_done_count((camping_diff_t)d));
    }
    lv_obj_t *hint = label(s_page, &lv_font_montserrat_14, C_LAKE);
    lv_obj_set_pos(hint, 12, 216);
    lv_label_set_text(hint, "UP/DOWN:SELECT  OK:OK");
}

static void page_difficulty_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   s_diff = (camping_diff_t)((s_diff + CAMPING_DIFF_COUNT - 1) % CAMPING_DIFF_COUNT);
    if (btn == BSP_BTN_DOWN) s_diff = (camping_diff_t)((s_diff + 1) % CAMPING_DIFF_COUNT);
    if (btn == BSP_BTN_OK) {
        // 从最新解锁的关卡开始
        s_level_index = camping_save_unlocked(s_diff) - 1;
        page_level_build();
        return;
    }
    page_difficulty_build();
}

// ---------- 关卡选择页 ----------
static void page_level_build(void)
{
    switch_page(PG_LEVEL);
    char buf[16];

    lv_obj_t *big = label(s_page, &lv_font_montserrat_20, C_WHITE);
    lv_obj_set_pos(big, 0, 40);
    lv_obj_set_width(big, 240);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(big, "%s LV %d", DIFF_NAME[s_diff], s_level_index + 1);

    lv_obj_t *best = label(s_page, &lv_font_montserrat_14, C_SUN);
    lv_obj_set_pos(best, 0, 76);
    lv_obj_set_width(best, 240);
    lv_obj_set_style_text_align(best, LV_TEXT_ALIGN_CENTER, 0);
    uint16_t sec = camping_save_best(s_diff, s_level_index);
    if (sec > 0) {
        format_mmss(sec, buf, sizeof(buf));
        lv_label_set_text_fmt(best, "BEST %s", buf);
    } else {
        lv_label_set_text(best, "NOT CLEARED");
    }

    lv_obj_t *prog = label(s_page, &lv_font_montserrat_14, C_LAKE);
    lv_obj_set_pos(prog, 0, 100);
    lv_obj_set_width(prog, 240);
    lv_obj_set_style_text_align(prog, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(prog, "DONE %d/200  UNLOCK %d",
                          camping_save_done_count(s_diff), camping_save_unlocked(s_diff));

    lv_obj_t *hint = label(s_page, &lv_font_montserrat_14, C_LAKE);
    lv_obj_set_pos(hint, 12, 216);
    lv_label_set_text(hint, "U/D:+-1 2xU/D:+-10 OK:PLAY");
}

static void page_level_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    int limit = camping_save_unlocked(s_diff);   // 可选 0..limit-1
    int step = (ev == BSP_BTN_DOUBLE) ? 10 : 1;
    if (ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) {
        if (btn == BSP_BTN_UP)   s_level_index -= step;
        if (btn == BSP_BTN_DOWN) s_level_index += step;
        if (s_level_index < 0) s_level_index = 0;
        if (s_level_index > limit - 1) s_level_index = limit - 1;
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) page_level_build();
    }
    if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        game_start();
    } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_DOWN) {
        page_difficulty_build();
    }
}

// ---------- 游戏页 ----------
static void game_analyze(void)
{
    s_analysis = camping_analyze(&s_board, current_level());
}

static void tick_clock(lv_timer_t *t)
{
    (void)t;
    if (!s_clock) return;
    char buf[8];
    format_mmss(elapsed_seconds(), buf, sizeof(buf));
    lv_label_set_text(s_clock, buf);
}

static void game_refresh(void)
{
    game_analyze();
    const camping_level_t *lv = current_level();

    // 光标位置 + 放置预览
    uint8_t cur_cell = camping_cell_at(s_cur_row, s_cur_col);
    uint8_t preview_cells[2];
    uint8_t preview_count = 0;
    bool preview = false;
    if (!camping_level_is_tree(lv, cur_cell) &&
        camping_can_place(&s_board, lv, s_sel_kind, cur_cell, s_sel_orient, -1)) {
        preview = camping_cells_for(s_sel_kind, cur_cell, s_sel_orient,
                                    preview_cells, &preview_count);
    }

    for (int i = 0; i < CAMPING_CELL_COUNT; i++) {
        lv_obj_t *cell = s_cells[i];
        lv_obj_t *deco = s_decos[i];
        int8_t piece = camping_piece_at(&s_board, (uint8_t)i);
        bool is_tree = camping_level_is_tree(lv, (uint8_t)i);

        if (is_tree) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(C_PINE), 0);
            lv_obj_set_size(deco, 14, 14);
            lv_obj_set_pos(deco, (CELL - 14) / 2, (CELL - 14) / 2);
            lv_obj_set_style_bg_color(deco, lv_color_hex(C_LAKE), 0);
            lv_obj_clear_flag(deco, LV_OBJ_FLAG_HIDDEN);
        } else if (piece >= 0) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(C_CORAL), 0);
            lv_obj_set_size(deco, 8, 8);
            lv_obj_set_pos(deco, (CELL - 8) / 2, (CELL - 8) / 2);
            lv_obj_set_style_bg_color(deco, lv_color_hex(C_WHITE), 0);
            lv_obj_clear_flag(deco, LV_OBJ_FLAG_HIDDEN);
            if (s_analysis.warnings & ((uint16_t)1u << piece)) {
                lv_obj_set_style_border_color(cell, lv_color_hex(C_RED), 0);
                lv_obj_set_style_border_width(cell, 2, 0);
            }
        } else {
            bool in_preview = false;
            for (uint8_t p = 0; p < preview_count; p++) {
                if (preview_cells[p] == i) in_preview = true;
            }
            if (in_preview && preview) {
                lv_obj_set_style_bg_color(cell, lv_color_hex(C_SUN), 0);
            } else {
                lv_obj_set_style_bg_color(cell, lv_color_hex(C_PAPER), 0);
            }
            lv_obj_add_flag(deco, LV_OBJ_FLAG_HIDDEN);
        }

        // 边框:默认 1px 墨色;警告帐篷 2px 红;光标 3px(非法为红)
        if (i == cur_cell) {
            lv_obj_set_style_border_color(cell, lv_color_hex(s_invalid ? C_RED : C_SUN), 0);
            lv_obj_set_style_border_width(cell, 3, 0);
        } else if (piece >= 0 && (s_analysis.warnings & ((uint16_t)1u << piece))) {
            lv_obj_set_style_border_color(cell, lv_color_hex(C_RED), 0);
            lv_obj_set_style_border_width(cell, 2, 0);
        } else {
            lv_obj_set_style_border_color(cell, lv_color_hex(C_INK), 0);
            lv_obj_set_style_border_width(cell, 1, 0);
        }
    }

    // 行列提示:超了红、正好绿、不够白
    for (int i = 0; i < CAMPING_SIZE; i++) {
        uint32_t rc = (s_analysis.rows[i] > lv->row_clues[i]) ? C_RED :
                      (s_analysis.rows[i] == lv->row_clues[i]) ? C_SUN : C_WHITE;
        uint32_t cc = (s_analysis.cols[i] > lv->col_clues[i]) ? C_RED :
                      (s_analysis.cols[i] == lv->col_clues[i]) ? C_SUN : C_WHITE;
        lv_obj_set_style_text_color(s_row_clues[i], lv_color_hex(rc), 0);
        lv_obj_set_style_text_color(s_col_clues[i], lv_color_hex(cc), 0);
        lv_label_set_text_fmt(s_row_clues[i], "%u", lv->row_clues[i]);
        lv_label_set_text_fmt(s_col_clues[i], "%u", lv->col_clues[i]);
    }

    // 库存:剩余数量 + 选中高亮
    int single_left = lv->singles - s_analysis.used_singles;
    int double_left = lv->doubles - s_analysis.used_doubles;
    lv_label_set_text_fmt(s_inv_labels[0], "1x TENT  %d", single_left);
    lv_label_set_text_fmt(s_inv_labels[1], "2x TENT  %d", double_left);
    lv_obj_set_style_border_width(s_inv_labels[0], 2, 0);
    lv_obj_set_style_border_width(s_inv_labels[1], 2, 0);
    lv_obj_set_style_border_color(s_inv_labels[0],
        lv_color_hex(s_sel_kind == CAMPING_TENT_SINGLE ? C_SUN : C_INK), 0);
    lv_obj_set_style_border_color(s_inv_labels[1],
        lv_color_hex(s_sel_kind == CAMPING_TENT_DOUBLE ? C_SUN : C_INK), 0);

    lv_label_set_text(s_help, s_invalid ? "2xOK:PUT  2xU/D:TENT"
                                        : "OK:RIGHT  2xOK:PUT  2xU/D:TENT");
}

static void game_start(void)
{
    switch_page(PG_GAME);
    camping_board_reset(&s_board);
    s_cur_row = s_cur_col = 0;
    s_invalid = false;

    const camping_level_t *lv = current_level();
    // 默认选第一种有库存的帐篷
    if (lv->singles > 0)      { s_sel_kind = CAMPING_TENT_SINGLE; s_sel_orient = CAMPING_ORIENT_H; }
    else if (lv->doubles > 0) { s_sel_kind = CAMPING_TENT_DOUBLE; s_sel_orient = CAMPING_ORIENT_H; }

    // 顶部信息条(挂在页容器上,随切页一起删除)
    s_level_tag = label(s_page, &lv_font_montserrat_14, C_LAKE);
    lv_obj_set_pos(s_level_tag, 8, 40);
    lv_label_set_text_fmt(s_level_tag, "%s LV %d", DIFF_NAME[s_diff], s_level_index + 1);
    s_clock = label(s_page, &lv_font_montserrat_14, C_WHITE);
    lv_obj_set_pos(s_clock, 192, 40);

    // 列提示(棋盘上方一行)
    for (int c = 0; c < CAMPING_SIZE; c++) {
        s_col_clues[c] = label(s_page, &lv_font_montserrat_14, C_WHITE);
        lv_obj_set_pos(s_col_clues[c], BOARD_X + c * CELL + 10, BOARD_Y - 16);
    }
    // 行提示(棋盘左侧一列)
    for (int r = 0; r < CAMPING_SIZE; r++) {
        s_row_clues[r] = label(s_page, &lv_font_montserrat_14, C_WHITE);
        lv_obj_set_pos(s_row_clues[r], 14, BOARD_Y + r * CELL + 6);
    }
    // 棋盘格
    for (int r = 0; r < CAMPING_SIZE; r++) {
        for (int c = 0; c < CAMPING_SIZE; c++) {
            int i = r * CAMPING_SIZE + c;
            s_cells[i] = block(s_page, BOARD_X + c * CELL, BOARD_Y + r * CELL, CELL, CELL, C_PAPER);
            lv_obj_set_style_border_color(s_cells[i], lv_color_hex(C_INK), 0);
            lv_obj_set_style_border_width(s_cells[i], 1, 0);
            s_decos[i] = block(s_cells[i], 8, 8, 8, 8, C_WHITE);
            lv_obj_add_flag(s_decos[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    // 库存两块
    for (int i = 0; i < 2; i++) {
        s_inv_labels[i] = label(s_page, &lv_font_montserrat_14, C_PINE);
        lv_obj_set_pos(s_inv_labels[i], 16 + i * 110, 254);
        lv_obj_set_size(s_inv_labels[i], 100, 22);
        lv_obj_set_style_bg_color(s_inv_labels[i], lv_color_hex(C_PAPER), 0);
        lv_obj_set_style_pad_all(s_inv_labels[i], 2, 0);
        lv_obj_set_style_radius(s_inv_labels[i], 0, 0);
    }
    // 操作提示
    s_help = label(s_page, &lv_font_montserrat_14, C_LAKE);
    lv_obj_set_pos(s_help, 8, 284);

    s_start_us = esp_timer_get_time();
    stop_timer();
    s_timer = lv_timer_create(tick_clock, 500, NULL);
    game_refresh();
}

// 通关结算页
static void page_clear_build(uint32_t seconds, uint16_t best_before)
{
    switch_page(PG_CLEAR);
    char buf[8];

    lv_obj_t *title = label(s_page, &lv_font_montserrat_20, C_SUN);
    lv_obj_set_pos(title, 0, 48);
    lv_obj_set_width(title, 240);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "CLEAR!");

    lv_obj_t *time = label(s_page, &lv_font_montserrat_14, C_WHITE);
    lv_obj_set_pos(time, 0, 84);
    lv_obj_set_width(time, 240);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_CENTER, 0);
    format_mmss(seconds, buf, sizeof(buf));
    lv_label_set_text_fmt(time, "TIME %s", buf);

    lv_obj_t *best = label(s_page, &lv_font_montserrat_14, C_LAKE);
    lv_obj_set_pos(best, 0, 108);
    lv_obj_set_width(best, 240);
    lv_obj_set_style_text_align(best, LV_TEXT_ALIGN_CENTER, 0);
    if (best_before == 0 || seconds < best_before) {
        lv_label_set_text(best, "NEW BEST!");
    } else {
        format_mmss(best_before, buf, sizeof(buf));
        lv_label_set_text_fmt(best, "BEST %s", buf);
    }

    lv_obj_t *hint = label(s_page, &lv_font_montserrat_14, C_LAKE);
    lv_obj_set_pos(hint, 24, 216);
    lv_label_set_text(hint, "OK:NEXT  DOWN-LONG:LEVELS");
}

// 通关:记录成绩并进结算页
static void game_complete(void)
{
    uint16_t seconds = (uint16_t)elapsed_seconds();
    uint16_t best_before = camping_save_best(s_diff, s_level_index);
    camping_save_complete(s_diff, s_level_index, seconds);
    ESP_LOGI(TAG, "通关 %s LV%d 用时 %us", DIFF_NAME[s_diff], s_level_index + 1, seconds);
    page_clear_build(seconds, best_before);
}

// 切换帐篷类型:单人 → 双人横 → 双人竖(跳过无库存的类型)
static void cycle_type(void)
{
    const camping_level_t *lv = current_level();
    uint8_t kind = s_sel_kind;
    uint8_t orient = s_sel_orient;
    for (int step = 0; step < 3; step++) {
        if (kind == CAMPING_TENT_SINGLE) { kind = CAMPING_TENT_DOUBLE; orient = CAMPING_ORIENT_H; }
        else if (orient == CAMPING_ORIENT_H) { orient = CAMPING_ORIENT_V; }
        else { kind = CAMPING_TENT_SINGLE; orient = CAMPING_ORIENT_H; }
        uint8_t limit = (kind == CAMPING_TENT_SINGLE) ? lv->singles : lv->doubles;
        uint8_t used = (kind == CAMPING_TENT_SINGLE) ? s_analysis.used_singles : s_analysis.used_doubles;
        if (used < limit) {
            s_sel_kind = kind;
            s_sel_orient = orient;
            return;
        }
    }
}

// 确定键双击:放置 / 拾起
static void game_action(void)
{
    const camping_level_t *lv = current_level();
    uint8_t cell = camping_cell_at(s_cur_row, s_cur_col);
    s_invalid = false;

    int8_t p = camping_piece_at(&s_board, cell);
    if (camping_level_is_tree(lv, cell)) {
        s_invalid = true;                                   // 树上不能放
    } else if (p >= 0) {
        // 拾起:取下跟随光标,确定当前朝向
        camping_piece_t piece = s_board.pieces[p];
        s_sel_kind = piece.kind;
        s_sel_orient = (piece.cell_count == 2 && piece.cells[1] - piece.cells[0] == CAMPING_SIZE)
                           ? CAMPING_ORIENT_V : CAMPING_ORIENT_H;
        camping_remove(&s_board, (uint8_t)p);
    } else if (!camping_place(&s_board, lv, s_sel_kind, cell, s_sel_orient)) {
        s_invalid = true;
    }

    game_refresh();
    if (s_analysis.valid) game_complete();
}

static void game_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    s_invalid = false;
    if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP)   s_cur_row = (s_cur_row + CAMPING_SIZE - 1) % CAMPING_SIZE;
        if (btn == BSP_BTN_DOWN) s_cur_row = (s_cur_row + 1) % CAMPING_SIZE;
        if (btn == BSP_BTN_OK)   s_cur_col = (s_cur_col + 1) % CAMPING_SIZE;
        game_refresh();
    } else if (ev == BSP_BTN_DOUBLE) {
        if (btn == BSP_BTN_OK) { game_action(); return; }
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) cycle_type();
        game_refresh();
    } else if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_UP) {                     // 清空重摆
            camping_board_reset(&s_board);
            game_refresh();
        } else if (btn == BSP_BTN_DOWN) {            // 回关卡选择
            page_level_build();
        }
    }
}

static void page_clear_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        int next = s_level_index + 1;
        if (next < CAMPING_LEVELS_PER_DIFFICULTY && next < camping_save_unlocked(s_diff)) {
            s_level_index = next;
            game_start();
        } else {
            page_level_build();
        }
    } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_DOWN) {
        page_level_build();
    }
}

// ---------- demo 框架接口 ----------
void demo_camping_enter(void)
{
    camping_save_init();
    s_diff = CAMPING_DIFF_EASY;
    s_level_index = 0;

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_PINE_D), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    lv_obj_t *title = label(s_scr, &lv_font_montserrat_20, C_LAKE);
    lv_obj_set_pos(title, 8, 10);
    lv_label_set_text(title, "CAMPING");

    s_page = NULL;
    page_difficulty_build();
    lv_screen_load(s_scr);
}

void demo_camping_exit(void)
{
    stop_timer();
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_page = NULL;
    s_clock = s_level_tag = s_help = NULL;
}

void demo_camping_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    switch (s_page_id) {
    case PG_DIFFICULTY: page_difficulty_key(btn, ev); break;
    case PG_LEVEL:      page_level_key(btn, ev); break;
    case PG_GAME:       game_key(btn, ev); break;
    case PG_CLEAR:      page_clear_key(btn, ev); break;
    }
}

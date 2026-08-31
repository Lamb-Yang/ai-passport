// main/camping_model.c —— 露营达人纯逻辑实现。规则见 camping_model.h 头注释。
#include "camping_model.h"

#include <string.h>

void camping_board_reset(camping_board_t *board)
{
    memset(board, 0, sizeof(*board));
}

bool camping_cells_for(uint8_t kind, uint8_t anchor, uint8_t orient,
                       uint8_t out_cells[2], uint8_t *out_count)
{
    if (anchor >= CAMPING_CELL_COUNT) return false;
    out_cells[0] = anchor;
    if (kind == CAMPING_TENT_SINGLE) {
        *out_count = 1;
        return true;
    }
    uint8_t row = camping_row_of(anchor);
    uint8_t col = camping_col_of(anchor);
    uint8_t second_row = row;
    uint8_t second_col = col;
    if (orient == CAMPING_ORIENT_H) {
        if (col + 1 >= CAMPING_SIZE) return false;
        second_col = col + 1;
    } else {
        if (row + 1 >= CAMPING_SIZE) return false;
        second_row = row + 1;
    }
    out_cells[1] = camping_cell_at(second_row, second_col);
    *out_count = 2;
    return true;
}

bool camping_can_place(const camping_board_t *board, const camping_level_t *level,
                       uint8_t kind, uint8_t anchor, uint8_t orient,
                       int8_t ignore_index)
{
    uint8_t cells[2];
    uint8_t count;
    if (!camping_cells_for(kind, anchor, orient, cells, &count)) return false;
    for (uint8_t i = 0; i < count; i++) {
        if (camping_level_is_tree(level, cells[i])) return false;
    }
    for (uint8_t p = 0; p < board->piece_count; p++) {
        if ((int8_t)p == ignore_index) continue;
        const camping_piece_t *piece = &board->pieces[p];
        for (uint8_t a = 0; a < piece->cell_count; a++) {
            for (uint8_t b = 0; b < count; b++) {
                if (piece->cells[a] == cells[b]) return false;
            }
        }
    }
    return true;
}

bool camping_place(camping_board_t *board, const camping_level_t *level,
                   uint8_t kind, uint8_t anchor, uint8_t orient)
{
    if (board->piece_count >= CAMPING_MAX_PIECES) return false;

    // 库存限制:单/双帐篷各自的数量不能超过关卡给定值。
    uint8_t used = 0;
    for (uint8_t p = 0; p < board->piece_count; p++) {
        if (board->pieces[p].kind == kind) used++;
    }
    uint8_t limit = (kind == CAMPING_TENT_SINGLE) ? level->singles : level->doubles;
    if (used >= limit) return false;

    if (!camping_can_place(board, level, kind, anchor, orient, -1)) return false;

    camping_piece_t *piece = &board->pieces[board->piece_count++];
    piece->kind = kind;
    camping_cells_for(kind, anchor, orient, piece->cells, &piece->cell_count);
    return true;
}

void camping_remove(camping_board_t *board, uint8_t piece_index)
{
    if (piece_index >= board->piece_count) return;
    memmove(&board->pieces[piece_index], &board->pieces[piece_index + 1],
            (board->piece_count - piece_index - 1) * sizeof(camping_piece_t));
    board->piece_count--;
}

int8_t camping_piece_at(const camping_board_t *board, uint8_t cell)
{
    for (uint8_t p = 0; p < board->piece_count; p++) {
        const camping_piece_t *piece = &board->pieces[p];
        for (uint8_t i = 0; i < piece->cell_count; i++) {
            if (piece->cells[i] == cell) return (int8_t)p;
        }
    }
    return -1;
}

// 两枚帐篷是否八方向接触(双帐篷自身两格除外,调用方只比较不同帐篷)。
static bool pieces_touch(const camping_piece_t *a, const camping_piece_t *b)
{
    for (uint8_t i = 0; i < a->cell_count; i++) {
        uint8_t ar = camping_row_of(a->cells[i]);
        uint8_t ac = camping_col_of(a->cells[i]);
        for (uint8_t j = 0; j < b->cell_count; j++) {
            uint8_t br = camping_row_of(b->cells[j]);
            uint8_t bc = camping_col_of(b->cells[j]);
            int dr = (int)ar - (int)br;
            int dc = (int)ac - (int)bc;
            if (dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1) return true;
        }
    }
    return false;
}

// 帐篷是否有至少一格与树正交相邻。
static bool piece_supported(const camping_piece_t *piece, const camping_level_t *level)
{
    static const int8_t DR[4] = { -1, 1, 0, 0 };
    static const int8_t DC[4] = { 0, 0, -1, 1 };
    for (uint8_t i = 0; i < piece->cell_count; i++) {
        int row = camping_row_of(piece->cells[i]);
        int col = camping_col_of(piece->cells[i]);
        for (uint8_t d = 0; d < 4; d++) {
            int nr = row + DR[d];
            int nc = col + DC[d];
            if (nr < 0 || nr >= CAMPING_SIZE || nc < 0 || nc >= CAMPING_SIZE) continue;
            if (camping_level_is_tree(level, camping_cell_at((uint8_t)nr, (uint8_t)nc))) {
                return true;
            }
        }
    }
    return false;
}

camping_analysis_t camping_analyze(const camping_board_t *board,
                                   const camping_level_t *level)
{
    camping_analysis_t a;
    memset(&a, 0, sizeof(a));

    for (uint8_t p = 0; p < board->piece_count; p++) {
        const camping_piece_t *piece = &board->pieces[p];
        for (uint8_t i = 0; i < piece->cell_count; i++) {
            a.rows[camping_row_of(piece->cells[i])]++;
            a.cols[camping_col_of(piece->cells[i])]++;
        }
        if (piece->kind == CAMPING_TENT_SINGLE) a.used_singles++;
        else a.used_doubles++;
        if (!piece_supported(piece, level)) a.warnings |= (uint16_t)1u << p;
    }
    for (uint8_t i = 0; i < board->piece_count; i++) {
        for (uint8_t j = (uint8_t)(i + 1); j < board->piece_count; j++) {
            if (pieces_touch(&board->pieces[i], &board->pieces[j])) {
                a.warnings |= (uint16_t)1u << i;
                a.warnings |= (uint16_t)1u << j;
            }
        }
    }

    a.inventory_complete = a.used_singles == level->singles && a.used_doubles == level->doubles;
    for (uint8_t i = 0; i < CAMPING_SIZE; i++) {
        if (a.rows[i] != level->row_clues[i] || a.cols[i] != level->col_clues[i]) {
            return a;   // clues_complete 保持 false
        }
    }
    a.clues_complete = true;
    a.valid = a.inventory_complete && a.clues_complete && a.warnings == 0;
    return a;
}

// main/camping_levels.h —— 露营达人关卡表声明。数据在 camping_levels.c(生成文件)。
#pragma once

#include "camping_model.h"

#define CAMPING_LEVELS_PER_DIFFICULTY 200

typedef enum {
    CAMPING_DIFF_EASY = 0,
    CAMPING_DIFF_MEDIUM,
    CAMPING_DIFF_HARD,
    CAMPING_DIFF_COUNT,
} camping_diff_t;

extern const camping_level_t CAMPING_LEVELS_EASY[CAMPING_LEVELS_PER_DIFFICULTY];
extern const camping_level_t CAMPING_LEVELS_MEDIUM[CAMPING_LEVELS_PER_DIFFICULTY];
extern const camping_level_t CAMPING_LEVELS_HARD[CAMPING_LEVELS_PER_DIFFICULTY];

// 按难度取关卡表。
const camping_level_t *camping_levels_of(camping_diff_t diff);

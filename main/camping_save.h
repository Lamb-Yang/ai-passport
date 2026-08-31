// main/camping_save.h —— 露营达人进度存档(NVS)。
// 每个难度存:解锁进度 + 每关最佳用时(秒)。NVS 不可用时退化为内存态,游戏仍可玩。
#pragma once

#include <stdint.h>

#include "camping_levels.h"

void camping_save_init(void);

// 当前可玩到的关卡数(1-based,顺序解锁)。失败时至少为 1。
uint8_t camping_save_unlocked(camping_diff_t diff);

// 某关最佳用时(秒),0 = 未通关。
uint16_t camping_save_best(camping_diff_t diff, int level_index);

// 已通关数量。
int camping_save_done_count(camping_diff_t diff);

// 通关:记录最佳用时并解锁下一关。
void camping_save_complete(camping_diff_t diff, int level_index, uint16_t seconds);

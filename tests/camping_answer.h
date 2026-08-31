// tests/camping_answer.h —— 关卡标准答案结构(仅宿主机测试使用)。
// 定义与 camping_answer_data.c(生成文件)保持一致。
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t single_cells[8];
    uint8_t single_count;
    uint8_t double_cells[4][2];
    uint8_t double_count;
} camping_answer_t;

extern const camping_answer_t CAMPING_ANSWERS_EASY[200];
extern const camping_answer_t CAMPING_ANSWERS_MEDIUM[200];
extern const camping_answer_t CAMPING_ANSWERS_HARD[200];

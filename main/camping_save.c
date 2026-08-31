// main/camping_save.c —— 露营达人进度存档实现。
// 数据放 "camping" 命名空间:每难度两个键(unlock u8 + best blob u16[200])。
// 参考 demo_radio 的取舍:NVS 初始化失败时不擦除分区,只退化为内存态。
#include "camping_save.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "camping_save";

static const char *const UNLOCK_KEY[CAMPING_DIFF_COUNT] = { "e_max", "m_max", "h_max" };
static const char *const BEST_KEY[CAMPING_DIFF_COUNT] = { "e_best", "m_best", "h_best" };

static nvs_handle_t s_nvs;
static bool s_ready;

// 内存缓存:初始化时读入,通关时写穿。u16 秒 + u8 进度 ×3 ≈ 1.2 KB,可接受。
static uint16_t s_best[CAMPING_DIFF_COUNT][CAMPING_LEVELS_PER_DIFFICULTY];
static uint8_t s_unlock[CAMPING_DIFF_COUNT];

void camping_save_init(void)
{
    s_unlock[0] = s_unlock[1] = s_unlock[2] = 1;
    memset(s_best, 0, sizeof(s_best));

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 初始化失败(%s),本次进度不落盘", esp_err_to_name(err));
        return;
    }
    err = nvs_open("camping", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开 camping 命名空间失败(%s),本次进度不落盘", esp_err_to_name(err));
        return;
    }
    s_ready = true;

    for (int d = 0; d < CAMPING_DIFF_COUNT; d++) {
        uint8_t unlock = 0;
        if (nvs_get_u8(s_nvs, UNLOCK_KEY[d], &unlock) == ESP_OK && unlock >= 1) {
            s_unlock[d] = unlock;
        }
        size_t len = sizeof(s_best[d]);
        if (nvs_get_blob(s_nvs, BEST_KEY[d], s_best[d], &len) != ESP_OK) {
            memset(s_best[d], 0, sizeof(s_best[d]));
        }
    }
}

uint8_t camping_save_unlocked(camping_diff_t diff)
{
    return s_unlock[diff];
}

uint16_t camping_save_best(camping_diff_t diff, int level_index)
{
    if (level_index < 0 || level_index >= CAMPING_LEVELS_PER_DIFFICULTY) return 0;
    return s_best[diff][level_index];
}

int camping_save_done_count(camping_diff_t diff)
{
    int count = 0;
    for (int i = 0; i < CAMPING_LEVELS_PER_DIFFICULTY; i++) {
        if (s_best[diff][i] != 0) count++;
    }
    return count;
}

void camping_save_complete(camping_diff_t diff, int level_index, uint16_t seconds)
{
    if (level_index < 0 || level_index >= CAMPING_LEVELS_PER_DIFFICULTY) return;

    uint16_t previous = s_best[diff][level_index];
    if (previous == 0 || seconds < previous) s_best[diff][level_index] = seconds;

    int next = level_index + 2;   // level_index 为 0-based,解锁到下一关
    if (next > s_unlock[diff] && next <= CAMPING_LEVELS_PER_DIFFICULTY) {
        s_unlock[diff] = (uint8_t)next;
    }
    if (!s_ready) return;

    nvs_set_blob(s_nvs, BEST_KEY[diff], s_best[diff], sizeof(s_best[diff]));
    nvs_set_u8(s_nvs, UNLOCK_KEY[diff], s_unlock[diff]);
    esp_err_t err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "进度写入失败:%s", esp_err_to_name(err));
    }
}

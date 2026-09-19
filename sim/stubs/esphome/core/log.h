// Host stand-in for ESPHome's logger. hb_ui.h and hb_parse.h use nothing but the four macros.
#pragma once
#include <cstdio>

#define HB_SIM_LOG(level, tag, fmt, ...) fprintf(stderr, "[" level "][%s] " fmt "\n", tag, ##__VA_ARGS__)

#define ESP_LOGE(tag, ...) HB_SIM_LOG("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) HB_SIM_LOG("W", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) HB_SIM_LOG("I", tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) HB_SIM_LOG("D", tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) HB_SIM_LOG("V", tag, __VA_ARGS__)

#pragma once
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
static int s_fake_mutex;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_fake_mutex; }
static inline int xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) { (void)h; (void)t; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t h) { (void)h; return 1; }

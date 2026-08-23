// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline int xTaskCreate(void (*fn)(void *), const char *n, unsigned s,
                              void *a, unsigned p, TaskHandle_t *h)
{ (void)fn; (void)n; (void)s; (void)a; (void)p; (void)h; return 1; }
static inline void vTaskDelete(TaskHandle_t h) { (void)h; }

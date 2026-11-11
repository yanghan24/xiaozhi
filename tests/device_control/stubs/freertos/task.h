#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*entry)(void*), const char* name, uint32_t stack, void* arg,
                unsigned priority, TaskHandle_t* handle);
void xTaskNotifyGive(TaskHandle_t handle);
uint32_t ulTaskNotifyTake(int clear, TickType_t ticks);
void vTaskDelete(TaskHandle_t handle);

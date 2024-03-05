#pragma once

#include <stdbool.h>

#include "common.h"

#define RING_SIZE 1024

struct RingsQueue;
typedef struct RingsQueue RingsQueue;

RingsQueue* RingsQueue_new(void);
void RingsQueue_delete(RingsQueue* queue);
void RingsQueue_push(RingsQueue* queue, Value item);
Value RingsQueue_pop(RingsQueue* queue);
bool RingsQueue_is_empty(RingsQueue* queue);

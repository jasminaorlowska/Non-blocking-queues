#pragma once

#include <stdbool.h>

#include "common.h"

#define BUFFER_SIZE 1024

struct BLQueue;
typedef struct BLQueue BLQueue;

BLQueue* BLQueue_new(void);
void BLQueue_delete(BLQueue* queue);
void BLQueue_push(BLQueue* queue, Value item);
Value BLQueue_pop(BLQueue* queued);
bool BLQueue_is_empty(BLQueue* queue);

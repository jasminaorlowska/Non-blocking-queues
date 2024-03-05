#pragma once

#include <stdbool.h>

#include "common.h"

struct LLQueue;
typedef struct LLQueue LLQueue;

LLQueue* LLQueue_new(void);
void LLQueue_delete(LLQueue* queue);
void LLQueue_push(LLQueue* queue, Value item);
Value LLQueue_pop(LLQueue* queue);
bool LLQueue_is_empty(LLQueue* queue);

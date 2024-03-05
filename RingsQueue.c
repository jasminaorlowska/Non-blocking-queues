#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>

#include <stdio.h>
#include <stdlib.h>

#include "HazardPointer.h"
#include "RingsQueue.h"

struct RingsQueueNode;
typedef struct RingsQueueNode RingsQueueNode;


struct RingsQueueNode {
    _Atomic(RingsQueueNode*) next;
    Value buffer[RING_SIZE];
    int push_idx; 
    int pop_idx; 
    _Atomic int free_slots; 
};

Value getValue(RingsQueueNode* node) {
    Value val = node->buffer[node->pop_idx];
    node->pop_idx = ((node->pop_idx + 1) % RING_SIZE);
    atomic_fetch_add(&(node->free_slots), 1);
    return val;
}

void pushValue(RingsQueueNode* node, Value val) {
    node->buffer[node->push_idx] = val;
    node->push_idx = ((node->push_idx + 1) % RING_SIZE);
    atomic_fetch_sub(&(node->free_slots), 1);
}

RingsQueueNode* RingsQueueNode_new() {
    RingsQueueNode* node = (RingsQueueNode*)malloc(sizeof(RingsQueueNode));
    assert(node != NULL);
    node->push_idx = 0; 
    node->pop_idx = 0;
    atomic_init(&node->free_slots, RING_SIZE); 
    atomic_init(&(node->next), NULL);
    return node; 
}

struct RingsQueue {
    RingsQueueNode* head;
    RingsQueueNode* tail;
    pthread_mutex_t pop_mtx;
    pthread_mutex_t push_mtx;
};

RingsQueue* RingsQueue_new(void) {
    RingsQueue* queue = (RingsQueue*)malloc(sizeof(RingsQueue));
    RingsQueueNode* node = RingsQueueNode_new();
    queue->head = node;
    queue->tail = node; 
    pthread_mutex_init(&queue->pop_mtx, NULL);
    pthread_mutex_init(&queue->push_mtx, NULL);
    return queue;
}

void RingsQueue_delete(RingsQueue* queue) {
    pthread_mutex_destroy(&queue->pop_mtx);
    pthread_mutex_destroy(&queue->push_mtx);
    RingsQueueNode* node = queue->head;
    while(node != NULL) {
        RingsQueueNode* next = atomic_load(&node->next);
        free(node);
        node = next;
    }
    free(queue);
}

RingsQueueNode* RingsQueueNode_new_with_value(Value val) {
    RingsQueueNode* node = (RingsQueueNode*)malloc(sizeof(RingsQueueNode));
    assert(node != NULL);
    node->buffer[0] = val;
    node->push_idx = 1; 
    node->pop_idx = 0; 
    atomic_init(&node->free_slots, RING_SIZE - 1);
    atomic_init(&(node->next), NULL);
    return node; 
}

void RingsQueue_push(RingsQueue* queue, Value item) {
    pthread_mutex_lock(&queue->push_mtx);

    if (atomic_load(&queue->tail->free_slots) > 0) {
        pushValue(queue->tail, item);
    }
    //Last node full. 
    else {
        RingsQueueNode* new_tail = RingsQueueNode_new_with_value(item);
        atomic_store(&queue->tail->next, new_tail);
        queue->tail = new_tail;
    }

   pthread_mutex_unlock(&queue->push_mtx);
}

Value RingsQueue_pop(RingsQueue* queue) {
    Value val = EMPTY_VALUE;

    pthread_mutex_lock(&(queue->pop_mtx));
    RingsQueueNode* head = queue->head; 

    //When head empty and has next node.
    if (atomic_load(&head->next) != NULL && 
        atomic_load(&head->free_slots) == RING_SIZE) {
            RingsQueueNode* new_head = atomic_load(&head->next);
            //Take the first element from node (new head). 
            free(head); 
            queue->head = new_head;
            val = getValue(new_head);
    }

    //Head not empty.
    else if (atomic_load(&head->free_slots) < RING_SIZE) {
        val = getValue(head);
    }

    //Head empty and no next node - return empty value. 

    pthread_mutex_unlock(&(queue->pop_mtx));
    return val;
}

bool RingsQueue_is_empty(RingsQueue* queue) {
    bool empty = true;
    pthread_mutex_lock(&(queue->pop_mtx));
    RingsQueueNode* head = queue->head; 
    if (atomic_load(&head->free_slots) < RING_SIZE || atomic_load(&head->next) != NULL) {
        empty = false;
    }
    pthread_mutex_unlock(&(queue->pop_mtx));
    return empty;
}

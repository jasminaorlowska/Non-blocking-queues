#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <assert.h>
#include "SimpleQueue.h"

struct SimpleQueueNode;
typedef struct SimpleQueueNode SimpleQueueNode;

struct SimpleQueueNode {
    _Atomic(SimpleQueueNode*) next;
    Value item;
};

SimpleQueueNode* SimpleQueueNode_new(Value item) {
    SimpleQueueNode* node = (SimpleQueueNode*)malloc(sizeof(SimpleQueueNode));
    assert(node != NULL); 
    atomic_init(&node->next, NULL);
    node->item = item; 
    return node;
}

struct SimpleQueue {
    SimpleQueueNode* head;
    SimpleQueueNode* tail;
    pthread_mutex_t head_mtx;
    pthread_mutex_t tail_mtx;
};

SimpleQueue* SimpleQueue_new(void)
{
    SimpleQueue* queue = (SimpleQueue*)malloc(sizeof(SimpleQueue));
    assert(queue != NULL);
    pthread_mutex_init(&queue->head_mtx, NULL);
    pthread_mutex_init(&queue->tail_mtx, NULL);
    SimpleQueueNode* node = SimpleQueueNode_new(EMPTY_VALUE);
    queue->head = node; 
    queue->tail = node;
    return queue;
}

//With guarantee that this operation is done at the end.
//Only one thread has access to the queue. 
void SimpleQueue_delete(SimpleQueue* queue) {
    pthread_mutex_destroy(&queue->head_mtx);
    pthread_mutex_destroy(&queue->tail_mtx);
    SimpleQueueNode* node = queue->head;
    while (node != NULL) {
        SimpleQueueNode* next = atomic_load(&node->next);
        free(node);
        node = next;
    }
    free(queue);
}

void SimpleQueue_push(SimpleQueue* queue, Value item) {
    SimpleQueueNode* new_node = SimpleQueueNode_new(item); 

    pthread_mutex_lock(&queue->tail_mtx); 
    atomic_store(&(queue->tail->next), new_node);
    queue->tail = new_node;
    pthread_mutex_unlock(&queue->tail_mtx); 
}

Value SimpleQueue_pop(SimpleQueue* queue) {
    pthread_mutex_lock(&queue->head_mtx);
    SimpleQueueNode* old_head = queue->head;  
    SimpleQueueNode* new_head = atomic_load(&(old_head->next));

    //No elements in the list.
    if (new_head == NULL) {
        pthread_mutex_unlock(&queue->head_mtx); 
        return EMPTY_VALUE;
    }
    //Get the value, replace old_head with new_value.
    Value val = new_head->item;
    queue->head = new_head;
    pthread_mutex_unlock(&queue->head_mtx); 
    //Free old
    free(old_head);
    return val;
}

bool SimpleQueue_is_empty(SimpleQueue* queue) {
    bool empty = false; 
    pthread_mutex_lock(&queue->head_mtx); 
    empty = (atomic_load(&(queue->head->next)) == NULL);
    pthread_mutex_unlock(&queue->head_mtx); 
    return empty;
}



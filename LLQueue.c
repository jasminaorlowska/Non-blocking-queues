//set follow-fork-mode child

#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <assert.h>
#include "HazardPointer.h"
#include "LLQueue.h"

struct LLNode;
typedef struct LLNode LLNode;
typedef _Atomic(LLNode*) AtomicLLNodePtr;

struct LLNode {
    AtomicLLNodePtr next;
    _Atomic Value item; 
};

LLNode* LLNode_new(Value item) {
    LLNode* node = (LLNode*)malloc(sizeof(LLNode));
    assert(node);
    atomic_init(&node->item, item);
    atomic_init(&node->next, NULL);
    return node;
}

struct LLQueue {
    AtomicLLNodePtr head;
    AtomicLLNodePtr tail;
    HazardPointer hp;
};


LLQueue* LLQueue_new(void) {
    LLQueue* queue = (LLQueue*)malloc(sizeof(LLQueue));
    assert(queue);
    HazardPointer_initialize(&queue->hp);
    //Head, tail initializing, dummy node with empty value at the beginning.
    AtomicLLNodePtr node = LLNode_new(EMPTY_VALUE);
    atomic_init(&(queue->head), node);
    atomic_init(&(queue->tail), node);

    return queue;
}

void LLQueue_delete(LLQueue* queue) {
    LLNode* curr = atomic_load(&(queue->head)), *next = NULL;
    // if (curr == NULL) printf("LLQueue_delete: head should never be NULL!");

    while (curr != NULL) {
        next = atomic_load(&curr->next);
        free(curr);
        curr = next;
    }

    HazardPointer_finalize(&queue->hp);
    free(queue);
}

void LLQueue_push(LLQueue* queue, Value item) {
    LLNode* new_node = LLNode_new(item);
    bool finished = false;
    while (!finished) {

        //Our expected tail will be protected.
        LLNode* expected_tail = HazardPointer_protect(&(queue->hp), (const _Atomic(void*)*)&(queue->tail));
        //if (expected_tail == NULL) printf("LLQueue_push: tail should never be NULL!");

        //If current tail = expected_tail, queue->tail will be changed to new_node
        if (atomic_compare_exchange_strong (&(queue->tail), &expected_tail, new_node)) {
            // if (atomic_load(&(expected_tail->next)) != NULL) printf("LLQueue_push: expected_tail->next should be NULL!\n");
            atomic_store(&(expected_tail->next), new_node);
            finished = true;
        }

        //Else: tail has changed, start again. 
    }
    
    HazardPointer_clear(&queue->hp);
}

Value LLQueue_pop(LLQueue* queue) {
    Value value = EMPTY_VALUE; //Here we will store head's item.
    bool finished = false;
    while (!finished) {
        value = EMPTY_VALUE;  

        //Our expected head will be protected.
        LLNode* expected_head = HazardPointer_protect(&queue->hp, (const _Atomic(void*)*)&(queue->head));
        //if (expected_head == NULL) printf("LLQueue_pop: head should never be NULL!");
        //Head has changed. Start again.
        if (expected_head != atomic_load(&(queue->head))) continue;

        value = atomic_exchange(&(expected_head->item), EMPTY_VALUE); //seg? 

        //We read actual pushed value.
        if (value != EMPTY_VALUE) {
            finished = true;
        }

        //Trying to move the head if has next.
        if (atomic_load(&(expected_head->next)) != NULL) {
            if (atomic_compare_exchange_strong(&(queue->head), &expected_head, atomic_load(&(expected_head->next)))) {
                HazardPointer_retire(&(queue->hp), expected_head);
            };
        }
        
        //Head next is NULL. Finishing with return value (modified or not). 
        else finished = true;
    }

    HazardPointer_clear(&(queue->hp));
    return value;
}

bool LLQueue_is_empty(LLQueue* queue) {
    Value value = EMPTY_VALUE; //Here we will store head's item.

    bool finished = false;
    while (!finished) {
        value = EMPTY_VALUE;  
        LLNode* expected_head = HazardPointer_protect(&(queue->hp), (const _Atomic(void*)*)&queue->head);
        // if (expected_head == NULL) printf("LLQueue_empty: head should never be NULL!");

        if (expected_head != atomic_load(&(queue->head))) continue;

        //Value was not empty value: queue not empty. Finishing.
        if ((value = atomic_load(&expected_head->item)) != EMPTY_VALUE) {
            finished = true;
        }

        //Value was empty value - checking whether we can move head.
        else {
            if (atomic_load(&(expected_head->next)) != NULL) {
                if (atomic_compare_exchange_strong(&(queue->head), &expected_head, atomic_load(&(expected_head->next)))) {
                        HazardPointer_retire(&(queue->hp), expected_head);
                };
            }
            //Head next is NULL. Finishing with return value == EMPTY_VALUE. 
            else finished = true;
        }
        
    }
    HazardPointer_clear(&(queue->hp));

    return value == EMPTY_VALUE;
}

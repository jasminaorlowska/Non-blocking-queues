#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include "BLQueue.h"
#include "HazardPointer.h"

struct BLNode;
typedef struct BLNode BLNode;
typedef _Atomic(BLNode*) AtomicBLNodePtr;

struct BLNode {
    AtomicBLNodePtr next;
    _Atomic Value buffer [BUFFER_SIZE];
    _Atomic int push_idx; 
    _Atomic int pop_idx; 
};

struct BLQueue {
    AtomicBLNodePtr head;
    AtomicBLNodePtr tail;
    HazardPointer hp;
};

//Creates new node with all values in buffer = EMPTY_VALUE.
BLNode* BLNode_new() {
    BLNode* node = (BLNode*)malloc(sizeof(BLNode));
    assert(node);

    atomic_init(&(node->push_idx), 0);
    atomic_init(&(node->pop_idx), 0);
    atomic_init(&(node->next), NULL);

    //All values in buffer are EMPTY_VALUE. 
    for (int i = 0; i < BUFFER_SIZE; i++) atomic_init(&(node->buffer[i]), EMPTY_VALUE);

    return node;
}

//Creates new node with first value in buffer = value, rest is EMPTY_VALUE.
BLNode* BLNode_new_with_value(Value value) {
    BLNode* node = (BLNode*)malloc(sizeof(BLNode));
    assert(node);

    atomic_init(&(node->push_idx), 1);
    atomic_init(&(node->pop_idx), 0);
    atomic_init(&(node->next), NULL);

    //First value in buffer is : value, rest is EMPTY_VALUE.
    atomic_init(&(node->buffer[0]), value);
    for (int i = 1; i < BUFFER_SIZE; i++) atomic_init(&(node->buffer[i]), EMPTY_VALUE);

    return node;
}

//Creates new BLQueue. Initializes its HazardPointer. 
BLQueue* BLQueue_new(void) {
    BLQueue* queue = (BLQueue*)malloc(sizeof(BLQueue));
    assert(queue);

    HazardPointer_initialize(&queue->hp);

    BLNode* node = BLNode_new();
    atomic_init(&(queue->head),node);
    atomic_init(&(queue->tail),node);

    return queue;
}

void BLQueue_delete(BLQueue* queue) {
    BLNode* curr = atomic_load(&(queue->head)), *next = NULL;
    //if (curr == NULL) printf("BLQueue_delete: Head should never be NULL!");

    while (curr != NULL) {
        next = atomic_load(&curr->next);
        free(curr);
        curr = next;
    }

    HazardPointer_finalize(&queue->hp);
    free(queue);
    queue = NULL;
}


void BLQueue_push(BLQueue* queue, Value item) {
    bool finished = false;
    while (!finished) { 

        BLNode* expected_tail = HazardPointer_protect(&(queue->hp), (const _Atomic(void*)*)&(queue->tail));
        //if (expected_tail == NULL) printf("BLQueue_push: tail should never be NULL!");

        //Start again tail has changed. 
        if (expected_tail != atomic_load(&(queue->tail))) continue; 

        int idx = atomic_fetch_add(&(expected_tail->push_idx), 1);
        
        //Buffer not full - we still can insert into it.
        if (idx < BUFFER_SIZE) {
            Value value_read = atomic_exchange(&expected_tail->buffer[idx], item); 
            if (value_read != TAKEN_VALUE) {
                //== EMPTY_VALUE, we inserted item.
                finished = true;
            }
            //Else: start again - value was already taken by pop-thread. 
        }

        //Buffer full. 
        else {  
            BLNode* next = atomic_load(&expected_tail->next);

            //Try to insert new tail (new node).
            if (next == NULL) { 
                BLNode* new_node = BLNode_new_with_value(item);
                if (!atomic_compare_exchange_strong(&(queue->tail), &expected_tail, new_node)) {
                    //Exchange unsuccessful, free new_node and start again. 
                    free(new_node);
                }
                else {
                    //Exchange successful, new tail set. Link old tail to new tail. 
                    if (atomic_load(&(expected_tail->next)) != NULL) printf("Push: expected_tail next should be NULL!\n");
                    atomic_store(&(expected_tail->next), new_node);
                    finished = true;
                }
            }

            //New tail already pushed. Try to change tail and start again.
            else  atomic_compare_exchange_strong(&(queue->tail), &expected_tail, next);
        }
    }
    HazardPointer_clear(&(queue->hp));
}

Value BLQueue_pop(BLQueue* queue) {
    Value value = EMPTY_VALUE; //Here we will store head's item.
    bool finished = false;

    while (!finished) {
        value = EMPTY_VALUE;  
        BLNode* expected_head = HazardPointer_protect(&queue->hp, (const _Atomic(void*)*)&(queue->head));
        //if (expected_head == NULL) printf("BLQueue_pop: head should never be NULL!");

        if (expected_head != atomic_load(&(queue->head))) continue;

        int idx = atomic_fetch_add(&(expected_head->pop_idx), 1);

        //Bufer not empty.
        if (idx < BUFFER_SIZE && idx >= 0) {
            value = atomic_exchange(&(expected_head->buffer[idx]), TAKEN_VALUE);  //seg?

            //We took pushed value. Finishing. 
            if (value != EMPTY_VALUE) {
                finished = true;
            }
            //Else: start again. 
        }

        //Buffer empty. 
        else {
            BLNode* next = atomic_load(&(expected_head->next)); 
            if (next == NULL) {
                //Queue empty. Finishing. 
                finished = true;
            }
            else {
                //Try to change the head.
                if (atomic_compare_exchange_strong(&(queue->head), &expected_head, next)) {
                    //If success - retire the old head. 
                    HazardPointer_retire(&queue->hp, expected_head);
                }
                //Start again. 
            }
        }
    }

    HazardPointer_clear(&(queue->hp));
    return value;
}

bool BLQueue_is_empty(BLQueue* queue) {
    Value value = EMPTY_VALUE; //Here we will store head's item.
    bool finished = false;

    while (!finished) {
        value = EMPTY_VALUE;  
        BLNode* expected_head = HazardPointer_protect(&queue->hp, (const _Atomic(void*)*)&(queue->head));
        //if (expected_head == NULL) printf("BLQueue_empty: head should never be NULL!");

        if (expected_head != atomic_load(&(queue->head))) continue;

        int idx = atomic_load(&(expected_head->pop_idx));

        //Bufer not empty.
        if (idx < BUFFER_SIZE) {
            value = atomic_load(&(expected_head->buffer[idx])); 

            if (value == EMPTY_VALUE) {
                //Queue empty.
                finished = true;
            }
            else if (value == TAKEN_VALUE) {
                //Someone popped a value in a meantime. Retrying. 
                continue;
            }
            else {
                //Queue not empty;
                finished = true;
            }
        }

        //Buffer empty. 
        else {
            BLNode* next = atomic_load(&(expected_head->next)); 
            if (next == NULL) {
                //Queue empty. Finishing. 
                finished = true;
            }
            else {
                //Try to change the head.
                if (atomic_compare_exchange_strong(&(queue->head), &expected_head, next)) {
                    //If success - retire the old head. 
                    HazardPointer_retire(&queue->hp, expected_head);
                }
                //Start again. 
            }
        }
    }

    HazardPointer_clear(&(queue->hp));
    return value == EMPTY_VALUE;
}

#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <assert.h>

#include "HazardPointer.h"

thread_local int _thread_id = -1;
int _num_threads = -1;

void HazardPointer_register(int thread_id, int num_threads) {
    _thread_id = thread_id;
    _num_threads = num_threads;
}

/*For each thread: creates and mallocs retired pointers list,
 initializes reserved pointer to NULL*/
void HazardPointer_initialize(HazardPointer* hp) {
    for (int i = 0; i < MAX_THREADS; i++) {

        //Initializing RetiredPointers Lists. 
        RetiredPointer_List* ret_ptr_list = (RetiredPointer_List*) malloc(sizeof(RetiredPointer_List));
        assert(ret_ptr_list);
        ret_ptr_list->head = NULL;
        ret_ptr_list->tail = NULL;
        ret_ptr_list->size = 0;
        hp->retired_ptrs[i] = ret_ptr_list;

        //Initializing all protected addresses to NULL; 
        atomic_init(&hp->pointer[i], NULL); 
    }
}

/*For each thread: free their retired ptrs, their retired ptrs list*/
void HazardPointer_finalize(HazardPointer* hp) {
    for (int i = 0; i < MAX_THREADS; i++) {
        RetiredPointer_List* ret_ptr_list = hp->retired_ptrs[i];
        RetiredPointer_Node* curr_node = ret_ptr_list->head, *tmp = NULL; 
        int counter = 0;

        while (curr_node != NULL) {
            tmp = curr_node->next;
            free(curr_node->pointer);
            curr_node->pointer = NULL;
            free(curr_node);
            curr_node = NULL;
            curr_node = tmp; 
            counter++;
        }

        //if (counter != ret_ptr_list->size) printf("HazardPointer_finalize: size wrong! Read: %d, should: %d\n", counter, ret_ptr_list->size);

        free(ret_ptr_list);
        ret_ptr_list = NULL;
    }
}

/*Protects a pointer (head or tail of the list). Returns the value of protected pointer.*/
void* HazardPointer_protect(HazardPointer* hp, const _Atomic(void*)* atom) {
    do {
        atomic_store(&hp->pointer[_thread_id], atomic_load(atom));
    } while (atomic_load(&hp->pointer[_thread_id]) != atomic_load(atom));
    
    return atomic_load(&hp->pointer[_thread_id]);
}

/*Removes a pointer from thread's protected pointer-value*/
void HazardPointer_clear(HazardPointer* hp) {
    atomic_store(&hp->pointer[_thread_id], NULL);  
}

RetiredPointer_Node* create_retired__node(void* ptr) {
    RetiredPointer_Node* node = (RetiredPointer_Node*) malloc(sizeof(RetiredPointer_Node));
    assert(node);
    node->pointer = ptr;
    node->next = NULL;
    return node;
}

void add_to_retired_list(HazardPointer* hp, void* ptr) {
    RetiredPointer_List* ret_ptr_list = hp->retired_ptrs[_thread_id];
    RetiredPointer_Node* new_node = create_retired__node(ptr); 

    if (ret_ptr_list->tail == NULL) {
        ret_ptr_list->head = new_node;
        ret_ptr_list->tail = new_node;
    }
    else {
        ret_ptr_list->tail->next = new_node;
        ret_ptr_list->tail = new_node;
    }

    ret_ptr_list->size++;
}

/*Returns true if any of thread is currently in use of this pointer.*/
bool can_free_node(HazardPointer* hp, void* ptr) {
    for (int i = 0; i < _num_threads; i++) {
        if (atomic_load(&hp->pointer[i]) == ptr) return false;
    }
    return true;
}

//Frees node not used by any other thread from retired pointers list.
void clean_retired_list(HazardPointer* hp) {
    RetiredPointer_Node *curr = hp->retired_ptrs[_thread_id]->head, *prev = NULL, *next = NULL; 

    //if (curr == NULL) printf("Clean_retired_list:, head should not be null!\n");
    //if (hp->retired_ptrs[_thread_id]->size != RETIRED_THRESHOLD) printf("Clean_retired_list: list-size should be: %d, is: %d\n", RETIRED_THRESHOLD, hp->retired_ptrs[_thread_id]->size);

    while (curr != NULL) {
        next = curr->next;
        
        if (can_free_node(hp, curr)) {
            hp->retired_ptrs[_thread_id]->size--;
            
            if (prev == NULL) hp->retired_ptrs[_thread_id]->head = next; 
            else prev->next = next;

            free(curr->pointer);
            curr->pointer = NULL;
            free(curr);
            curr = next;
        }

        else {
            prev = curr;
            curr = next; 
        }
    }

    hp->retired_ptrs[_thread_id]->tail = prev; 
}

void HazardPointer_retire(HazardPointer* hp, void* ptr) {
    RetiredPointer_List* ret_ptr_list = hp->retired_ptrs[_thread_id];
    if (ret_ptr_list->size == RETIRED_THRESHOLD) {
        //Too many ptrs on retired list - list should be cleaned. 
        clean_retired_list(hp);
    }
    add_to_retired_list(hp, ptr);
}

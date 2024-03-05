# Implementation of several non-blocking queues with multiple readers and writers:
- SimpleQueue,
- RingsQueue,
- LLQueue,
-  BLQueue.

Two implementations will use regular mutexes, and the other two will use atomic operations, including the key compare_exchange operation.

In each case, the implementation consists of a structure <queue> and methods:
- `<queue>* <queue>_new(void)` - allocates (malloc) and initializes a new queue.
- `void <queue>_delete(<queue>* queue)` - frees any memory allocated by the queue methods.
- `void <queue>_push(<queue>* queue, Value value)` - adds a value to the end of the queue.
- `Value <queue>_pop(<queue>* queue)` - retrieves a value from the beginning of the queue or returns EMPTY_VALUE if the queue is empty.
- `bool <queue>_is_empty(<queue>* queue)` - checks if the queue is empty.

For example, the first implementation should define the structure SimpleQueue and the methods SimpleQueue* SimpleQueue_new(void), etc.

The values in the queue have a Value type equal to int64_t (for convenient testing, we would normally hold void* there).
Queue users cannot use the values EMPTY_VALUE=0 or TAKEN_VALUE=-1 (these can be used as special values).
Queue users should perform new/delete exactly once, respectively before/after all push/pop/is_empty operations from all threads.

All implementations behave safely as if the push, pop, and is_empty operations were indivisible
(from the perspective of the values returned to queue users).
Implementations does not fully guarantee fairness and lack of starvation for each thread individually. 
For example, it is allowed for one thread performing push to be starved, as long as other threads manage to complete their pushes.

Lock-free queue implementations (LLQueue and BLQueue) guarantee that in each parallel execution of operations:
- at least one push operation will complete in a finite number of steps (bounded by a constant dependent on the number of threads and buffer sizes in BLQueue),
- at least one pop operation will complete in a finite number of steps, and
- at least one is_empty operation will complete in a finite number of steps if such operations have started and have not been suspended.
This guarantee holds even if other threads are randomly suspended for a longer period (e.g., due to preemption).

# SimpleQueue
**Implemented using a singly linked list with two mutexes.**

This is one of the simpler implementations of a queue. 
Having separate mutexes for producers and consumers allows for better parallelization of operations.

The structure of SimpleQueue consists of:

- a singly linked list of nodes, where each node contains:
- an atomic pointer next to the next node in the list,
- a value of type Value;
- a pointer head to the first node in the list, along with a mutex to protect access to it;
- a pointer tail to the last node in the list, along with a mutex to protect access to it.

# RingsQueue
**Implemented using a combination of SimpleQueue and a queue implemented on a circular buffer,
combining the unlimited size of the former with the efficiency of the latter (singly linked lists are relatively slow due to continuous memory allocations).**

The structure of RingsQueue consists of:

- a singly linked list of nodes, where each node contains:
    - an atomic pointer next to the next node in the list,
    - a circular buffer in the form of an array of RING_SIZE values of type Value,
    - an atomic counter push_idx for the number of push operations performed on this node,
    - an atomic counter pop_idx for the number of pop operations performed on this node.
- a pointer head to the first node in the list;
- a pointer tail to the last node in the list (head and tail may point to the same node);
- a mutex pop_mtx to lock the entire pop operation;
- a mutex push_mtx to lock the entire push operation.
  Total number of push and pop/is_empty operations should be at most 2^60.
  The constant RING_SIZE is defined in RingsQueue.h and is set to 1024.
  Push and pop/is_empty operations proceeds concurrently, i.e., suspending a thread performing push does not block a thread performing pop/is_empty,
- and suspending a thread performing pop/is_empty does not block a thread performing push.

**Push works as follows:**
- If the node pointed to by tail is not full, it adds a new value to its circular buffer, incrementing push_idx.
- If it is full, it creates a new node and updates the appropriate pointers.

**Pop works as follows:**
- If the node pointed to by head is not empty, it returns a value from its circular buffer, incrementing pop_idx.
- If this node is empty and there is no successor: it returns EMPTY_VALUE.
- If this node is empty and has a successor: it updates head to the next node and returns a value from its circular buffer.

# LLQueue
**Lock-free queue implemented using a singly linked list**

This is one of the simplest lock-free queue implementations.

The structure of LLQueue consists of:
- a singly linked list of nodes, where each node contains:
    - an atomic pointer next to the next node in the list,
    - a value of type Value, equal to EMPTY_VALUE if the value from the node has already been retrieved;
- an atomic pointer head to the first node in the list;
- an atomic pointer tail to the last node in the list;
- a HazardPointer structure (see below).
  
**Push works in a loop trying to perform the following steps:**
- Read the pointer to the last node of the queue (by the thread executing push).
- Swap the successor with a new node containing our element.
- If successful:
    - Update the pointer to the last node in the queue to our new node and exit the function.
- If unsuccessful (another thread has already extended the list), retry everything from the beginning, ensuring that the last node has been updated.

**Pop works in a loop trying to perform the following steps:**
- Read the pointer to the first node of the queue.
- Read the value from this node and set it to EMPTY_VALUE.
- If the read value was different from EMPTY_VALUE:
    - Update the pointer to the first node (if necessary) and return the result.
- If the read value was EMPTY_VALUE, check if the queue is empty.
    - If it is, return EMPTY_VALUE, and if not, retry everything from the beginning, ensuring that the first node has been updated.


# BLQueue:
**lock-free queue implemented using a list of buffers**

This is one of the simpler yet highly efficient implementations of a queue.
The idea behind this queue is to combine a singly linked list solution with a solution where the queue is a simple array
with atomic indices for inserting and retrieving elements (but the number of operations would be limited by the length of the array). 
We combine the advantages of both by making a list of arrays; we only need to transition to a new list node when the array is full. 
However, the array here is not a circular buffer; 
each field in it is filled at most once (a variant with circular buffers would be much more difficult).

The structure of BLQueue consists of:

- a singly linked list of nodes, where each node contains:
  - an atomic pointer next to the next node in the list,
  - a buffer with BUFFER_SIZE atomic values of type Value,
  - an atomic index push_idx for the next place in the buffer to be filled by push (increases with each push attempt),
  - an atomic index pop_idx for the next place in the buffer to be emptied by pop (increases with each pop attempt);
- an atomic pointer head to the first node in the list;
- an atomic pointer tail to the last node in the list;
- a HazardPointer structure (see below).

The queue initially contains one node with an empty buffer. 
The elements of the buffer initially have the value EMPTY_VALUE.
Pop operations will change retrieved or empty values to TAKEN_VALUE (allowing pop to occasionally waste elements of the array in this way).
The constant BUFFER_SIZE is defined in BLQueue.h and is set to 1024, but it can be changed to a smaller power of two greater than 2 in tests.

**Push works in a loop trying to perform the following steps:**
- Read the pointer to the last node of the queue.
- Retrieve and increment from this node the index of the place in the buffer to be filled by push (no other thread will try to push into this place).
- If the index is less than the size of the buffer, try to insert the element into this place in the buffer.
  - If another thread managed to change this place (another thread executing pop might have changed it to TAKEN_VALUE), retry everything from the beginning.
  - If we managed to insert the element, exit the function.
- If the index is greater than or equal to the size of the buffer, it means that the buffer is full, and we will need to create or move to the next node.
To do this, first check if the next node has already been created.
  - If so, ensure that the pointer to the last node in the queue has changed and retry everything from the beginning.
  - If not, create a new node with our single element in the buffer.
   Try to insert the pointer to the new node as the successor.
    - If unsuccessful (another thread managed to extend the list), remove our node and retry everything from the beginning.
    - If successful, update the pointer to the last node in the queue to our new node.

**Pop works in a loop trying to perform the following steps:**

- Read the pointer to the first node of the queue.
- Retrieve and increment from this node the index of the place in the buffer to be read by pop (no other thread will try to pop from this place).
- If the index is less than the size of the buffer, read the element from this place in the buffer and replace it with TAKEN_VALUE.
  - If we retrieved EMPTY_VALUE, retry everything from the beginning.
  - If we retrieved another element, exit the function.
- If the index is greater than or equal to the size of the buffer, it means that the buffer is completely empty, and we will need to move to the next node. 
  First, check if the next node has already been created.
  - If not, the queue is empty, exit the function.
  - If so, ensure that the pointer to the first node in the queue has changed and retry everything from the beginning.


# Hazard Pointer
Hazard Pointer is a technique used to handle the problem of safely releasing memory in data structures shared by multiple threads
and to address the ABA problem. 
The idea is that each thread can reserve one address for a node (one for each instance of the queue)
that it needs to protect from deletion (or ABA replacement) during push/pop/is_empty operations. 
Instead of freeing a node (free()), a thread adds its address to its set of retired addresses and periodically reviews this set, freeing addresses that are not reserved.

The HazardPointer structure should consist of:

- an array containing an atomic pointer for each thread – containing the "reserved" address of the node by the thread;
- an array of sets of pointers for each thread – addresses "retired" (withdrawn) for later release;
  
The implementation consists of the following methods:
- `void HazardPointer_register(int thread_id, int num_threads)` – registers a thread with the identifier thread_id.
- `void HazardPointer_initialize(HazardPointer* hp)` – initializes the structure (already allocated): all reserved addresses are set to NULL.
- `void HazardPointer_finalize(HazardPointer* hp)` – clears all reservations, frees memory allocated by the structure's methods, and releases all addresses from the retired array (does not free the HazardPointer structure itself).
- `void* HazardPointer_protect(HazardPointer* hp, const AtomicPtr* atom)` – saves the address read from atom in the reserved addresses array at the index thread_id and returns it (overwriting an existing reservation if there was one for thread_id).
- `void HazardPointer_clear(HazardPointer* hp)` – removes the reservation, i.e., sets the address at the index thread_id to NULL.
- `void HazardPointer_retire(HazardPointer* hp, void* ptr)` – adds ptr to the set of retired addresses, for which the thread with thread_id is responsible for freeing. Then, if the size of the retired set exceeds the threshold defined by the constant RETIRED_THRESHOLD (e.g., MAX_THREADS), it reviews all addresses in its set and frees (free()) those that are not reserved by any thread (also removing them from the set).

Users of queues using HazardPointer have to guarantee that each thread will call HazardPointer_register with a unique thread_id
(an integer from the range [0, num_threads)) before performing any push/pop/is_empty operation on the queue, 
with the same num_threads for all threads. 

The function `<queue>_new` (thus also HazardPointer_initialize) can be called by users before HazardPointer_register. 


**ATTENTION**: 

num_threads <= MAX_THREADS, where MAX_THREAD = 128.

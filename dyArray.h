// Author: AJ Tomalino
// File: dyArray.h
// Purpose: dynamic array implementation
// implementation in dyArray.c

// Sources:
// https://medium.com/@anthonybaxter819/dynamic-array-in-c-a827b9cd56ef
// https://navaneethkn.wordpress.com/2012/05/11/simple-generic-and-dynamically-allocated-array-in-c/
// https://dev.to/pauljlucas/a-simple-dynamic-array-for-c-1k24

#ifndef DYARRAY_H
#define DYARRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULTSIZE 8

typedef struct {
  void *arr;
  size_t esize;
  size_t len;
  size_t cap;
} dyArray;

// Create a dyArray object with default values
// default - len: 0
//           cap: DEFAULTSIZE
//           arr: pointer to allocated memory of DEFAULTSIZE * _size
// inputs: _size, byte size of element type
//                esize set to _size
// outputs: dyArray with default values
dyArray array_init(size_t _size);

// Access element at index
// inputs: dy, array to access
//         idx, index to get
// outputs: void pointer to element
void *array_at(dyArray *dy, size_t idx);

// Append element to end of array
// inputs: dy, array to access
//         ele, element to append
// outputs: true if operation successful, false otherwise
// postcondition: uses _array_realloc to expand array if array is full
//                len incremented
bool array_push(dyArray *dy, void *ele);

// Insert element at index
// inputs: dy, array to access
//         ele, element to insert
//         idx, index to insert at
// outputs: true if operation successful, false otherwise
// postcondition: uses _array_realloc to expand array if array is full
//                len incremented
bool array_insert(dyArray *dy, void *ele, size_t idx);

// Remove element at index
// inputs: dy, array to access
//         idx, index of element to remove
// postcondition: uses _array_realloc to downsize array if len is under half
//                of cap
//                len decremented
void array_rmv(dyArray *dy, size_t idx);

// Free array memory
// inputs: dy, array to free
// postcondition: arr is free from heap
void array_free(dyArray *dy);

// Reallocate heap memory
// inputs: arr, raw array pointer to realloc
//         new_size, byte size to change to
// postcondition: arr is of size new_size
//                if new_size is 0, free arr
void *_array_realloc(void *arr, size_t new_size);

#endif // !DYARRAY

#include "dyArray.h"

dyArray array_init(size_t _size) {
  dyArray dy;
  dy.esize = _size;
  dy.len = 0;
  dy.cap = DEFAULTSIZE;
  dy.arr = malloc(DEFAULTSIZE * _size);
  if (dy.arr == NULL) {
    dy.cap = 0;
  }
  // if cap is 0, then there is an error
  return dy;
}

void *array_at(dyArray *dy, size_t idx) {
  // return null if array is null or idx out of bounds
  if (dy == NULL || dy->arr == NULL || idx < 0 || (size_t)idx >= dy->len) {
    return NULL;
  }
  // cast as a char* to allow pointer arithmetic
  return (char *)dy->arr + idx * dy->esize;
}

bool array_push(dyArray *dy, void *ele) {
  if (dy == NULL || dy->arr == NULL || ele == NULL) {
    return false;
  }
  // expand if full
  if (dy->len == dy->cap) {
    size_t new_cap = dy->cap * 2;
    void *tmp = _array_realloc(dy->arr, new_cap * dy->esize);
    if (tmp == NULL) {
      return false;
    }
    dy->arr = tmp;
    dy->cap = new_cap;
  }
  // add element
  memcpy((char *)dy->arr + dy->len++ * dy->esize, ele, dy->esize);
  return true;
}

bool array_insert(dyArray *dy, void *ele, size_t idx) {
  if (dy == NULL || dy->arr == NULL || ele == NULL || idx < 0 ||
      idx > dy->len) {
    return false;
  }
  // same expand if full
  if (dy->len == dy->cap) {
    size_t new_cap = dy->cap * 2;
    void *tmp = _array_realloc(dy->arr, new_cap * dy->esize);
    if (tmp == NULL) {
      return false;
    }
    dy->arr = tmp;
    dy->cap = new_cap;
  }
  char *base = (char *)dy->arr; // copy of array
  // shift array back
  memmove(base + (idx + 1) * dy->esize, base + idx * dy->esize,
          (dy->len - idx) * dy->esize);
  // add element to array
  memcpy(base + idx * dy->esize, ele, dy->esize);
  dy->len++;
  return true;
}

void array_rmv(dyArray *dy, size_t idx) {
  if (dy == NULL || dy->arr == NULL || idx < 0 || idx >= dy->len) {
    return;
  }
  char *base = (char *)dy->arr;
  // shift array forward
  memmove(base + idx * dy->esize, base + (idx + 1) * dy->esize,
          (dy->len - idx - 1) * dy->esize);
  dy->len--;
  // shrink if under half
  if (dy->len < dy->cap / 2 && dy->cap / 2 >= 1) {
    size_t new_cap = dy->cap / 2;
    void *tmp = _array_realloc(dy->arr, new_cap * dy->esize);
    if (tmp != NULL) {
      dy->arr = tmp;
      dy->cap = new_cap;
    }
  }
}

void array_free(dyArray *dy) {
  if (dy == NULL) {
    return;
  }
  free(dy->arr);
  dy->arr = NULL;
  dy->len = 0;
  dy->cap = 0;
}

void *_array_realloc(void *arr, size_t new_size) {
  if (new_size == 0) {
    free(arr);
    return NULL;
  }
  return realloc(arr, new_size);
}

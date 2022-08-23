#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "allocdbg.h"

#define ARENAMAX 128
#define ARENASIZE (1024*1024)
#define ALIGNMENT 8

struct freelist {
	int next;
	int size;
};

struct arena {
	uint8_t * mem;
	struct freelist *list;
};

struct allocdbg {
	int mem;
	int n;
	size_t hugemem;
	size_t peakmem;
	struct arena a[ARENAMAX];
};

static inline int
alignment(int sz) {
	return (sz + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

struct allocdbg *
allocdbg_new() {
	struct allocdbg *A = (struct allocdbg *)malloc(sizeof(*A));
	memset(A, 0, sizeof(*A));
	return A;
}

void
allocdbg_delete(struct allocdbg *A) {
	if (A == NULL)
		return;
	assert(A->n == 0);
	assert(A->hugemem == 0);
	assert(A->mem == 0);
	int i;
	for (i=0;i<ARENAMAX;i++) {
		uint8_t * ptr = A->a[i].mem;
		if (ptr)
			free(ptr);
		else
			break;
	}
	free(A);
}

static void *
arena_new(struct arena *a, int sz) {
	a->mem = (uint8_t *)malloc(ARENASIZE);
	sz = alignment(sz);
	if (sz >= ARENASIZE) {
		assert(sz == ARENASIZE);
		a->list = NULL;
		return a->mem;
	}
	int off = ARENASIZE - sz;
	struct freelist * f = (struct freelist *)a->mem;
	f->next = -1;
	f->size = off;
	a->list = f;
	return a->mem + off;
}

static void *
arena_alloc(struct arena *a, int sz) {
	sz = alignment(sz);
	struct freelist * f = a->list;
	if (f == NULL)
		return NULL;
	if (f->size == sz) {
		if (f->next < 0) {
			a->list = NULL;
		} else {
			a->list = (struct freelist *)(a->mem + f->next);
		}
		return f;
	}
	struct freelist * bestfit = NULL;
	struct freelist * prev = NULL;
	int fitsize = ARENASIZE + 1;
	for (;;) {
		if (sz <= f->size) {
			if (sz == f->size) {
				prev->next = f->next;				
				return f;
			}
			if (f->size < fitsize) {
				bestfit = f;
			}
		}
		if (f->next < 0) {
			break;
		} else {
			prev = f;
			f = (struct freelist *)(a->mem + f->next);
		}
	}
	if (bestfit) {
		int off = bestfit->size - sz;
		bestfit->size -= sz;
		return (uint8_t *)bestfit + off;
	}
	return NULL;
}

static void
sort_arena_(struct allocdbg *A, int index) {
	struct arena a = A->a[index];
	int begin = 0;
	int end = index;
	while (begin < end) {
		int mid = (begin + end) / 2;
		uint8_t *ptr = A->a[mid].mem;
		if (a.mem < ptr) {
			end = mid;
		} else {
			assert(a.mem > ptr);
			begin = mid + 1;
		}
	}
	// insert at begin
	if (begin == index)
		return;
	memmove(A->a + begin + 1, A->a + begin , (index - begin) * sizeof(struct arena));
	A->a[begin] = a;
}

static inline void
dump_arena(struct arena *a, int index, int sz) {
	struct freelist *f = a->list;
	if (f) {
		int off = (uint8_t *)f - a->mem;
		printf("Request %d %d:", index, sz);
		while (f) {
			printf("%d", f->size);
			if (f->next < 0)
				break;
			printf("[%d] ", f->next - off);
			off = f->next;
			f = (struct freelist *)(a->mem + off);
		}
		printf("\n");
	}
}


static void *
alloc_(struct allocdbg *A, int sz) {
	int i;
	for (i=0;i<ARENAMAX;i++) {
		struct arena * a = &A->a[i];
		if (a->mem == NULL) {
//			printf("mem = %d peak = %d index = %d\n", A->mem, (int)A->peakmem, i);
			void *ret = arena_new(a, sz);
			if (ret == NULL)
				return NULL;
			sort_arena_(A, i);
			return ret;
		} else {
			void * ret = arena_alloc(a, sz);
			if (ret)
				return ret;
//			dump_arena(a, i, sz);
		}
	}
	return NULL;
}

static struct arena *
arena_find(struct allocdbg *A, void *ptr) {
	int begin = 0;
	int end = ARENAMAX;
	uint8_t * p = (uint8_t *)ptr;
	while (begin < end) {
		int mid = (begin + end) / 2;
		uint8_t * m = A->a[mid].mem;
		if (m == NULL || p < m) {
			end = mid;
		} else if (p >= m && p < m + ARENASIZE) {
			return &A->a[mid];
		} else {
			begin = mid + 1;
		}
	}
	return NULL;
}

static void
dealloc_(struct allocdbg *A, void *ptr, int osize) {
	struct arena *a = arena_find(A, ptr);
	assert(a);
	osize = alignment(osize);
	int off = (uint8_t *)ptr - a->mem;
	assert(off + osize <= ARENASIZE);
	struct freelist *f = (struct freelist *)ptr;
	struct freelist *head = a->list;
	if (head == NULL) {
		f->next = -1;
		f->size = osize;
		a->list = f;
		return;
	}
	uint8_t *u8ptr = (uint8_t *)ptr;
	struct freelist *endptr = (struct freelist *)(u8ptr + osize);
	struct freelist *prev = NULL;
	struct freelist *current = head;
	for (;;) {
		if (f < current) {
			if (endptr == current) {
				// merge before current
				f->size = current->size + osize;
				f->next = current->next;
			} else {
				// insert before current
				f->size = osize;
				f->next = (uint8_t *)current - a->mem;
			}
			if (prev) {
				prev->next = off;
			} else {
				a->list = f;
			}
			return;
		}
		struct freelist *next = NULL;
		if (current->next >= 0) {
			next = (struct freelist *)(a->mem + current->next);
		}
		uint8_t * current_endptr = (uint8_t *)current + current->size;
		if (u8ptr <= current_endptr) {
			assert(u8ptr == current_endptr);
			// merge after current
			current->size += osize;
			if (endptr == next) {
				current->size += next->size;
				current->next = next->next;
			}
			return;
		}
		if (next == NULL) {
			// insert end
			f->size = osize;
			f->next = -1;
			current->next = off;
			return;
		}
		prev = current;
		current = next;
	}
}

void *
allocdbg(void *ud, void *ptr, size_t osize, size_t nsize) {
	struct allocdbg *A = (struct allocdbg *)ud;
	if (nsize == 0) {
		// free(ptr)
		if (osize > ARENASIZE) {
			free(ptr);
			A->hugemem -= osize;
		} else if (ptr == NULL) {
			// free(NULL);
			return NULL;
		} else {
			dealloc_(A, ptr, osize);
			A->mem -= osize;
		}
		A->n--;
		return NULL;
	} else if (ptr == NULL) {
		// malloc(nsize)
		A->n++;
		if (nsize > ARENASIZE) {
			A->hugemem += nsize;
			return malloc(nsize);
		} else {
			A->mem += nsize;
			if (A->mem > A->peakmem) {
				A->peakmem = A->mem;
			}
			return alloc_(A, nsize);
		}
	} else {
		// realloc(ptr, nsize)
		if (osize <= ARENASIZE && nsize <= ARENASIZE) {
			void * ret = alloc_(A, nsize);
			if (ret == NULL)
				return NULL;
			memcpy(ret, ptr, (osize < nsize ? osize : nsize));
			dealloc_(A, ptr, osize);
			A->mem -= osize;
			A->mem += nsize;
			if (A->mem > A->peakmem) {
				A->peakmem = A->mem;
			}
			return ret;
		}
		if (osize > ARENASIZE && nsize > ARENASIZE) {
			A->hugemem -= osize;
			A->hugemem += nsize;
			return realloc(ptr, nsize);
		}
		if (osize <= ARENASIZE && nsize > ARENASIZE) {
			void * ret = malloc(nsize);
			if (ret == NULL)
				return NULL;
			memcpy(ret, ptr, osize);
			dealloc_(A, ptr, osize);
			A->mem -= osize;
			A->hugemem += nsize;
			return ret;
		} else {
			assert(osize > ARENASIZE && nsize <= ARENASIZE);
			void * ret = alloc_(A, nsize);
			if (ret == NULL)
				return NULL;
			memcpy(ret, ptr, nsize);
			free(ptr);
			A->hugemem -= osize;
			A->mem += nsize;
			return ret;
		}
	}
}

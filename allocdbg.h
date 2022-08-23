#ifndef alloc_lua_debug_h
#define alloc_lua_debug_h

struct allocdbg;

struct allocdbg * allocdbg_new();
void allocdbg_delete(struct allocdbg *A);
void * allocdbg(void *ud, void *ptr, size_t osize, size_t nsize);

#endif

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>

#include "allocdbg.h"

int
main(int argc, char *argv[]) {
	if (argc != 2)
		return 1;
	struct allocdbg *  A = allocdbg_new();
	lua_State *L = lua_newstate(allocdbg, A);
	luaL_openlibs(L);
	if (luaL_dofile(L, argv[1]) != LUA_OK) {
		printf("Error: %s\n", lua_tostring(L, -1));
	}

	lua_close(L);

	allocdbg_delete(A);


	return 0;
}
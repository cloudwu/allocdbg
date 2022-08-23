LUAINC=-I /usr/local/include
LUALIB=-L /usr/local/bin -llua54
CFLAGS=-g -Wall

all : testlua.exe

testlua.exe : testlua.c allocdbg.c
	gcc $(CFLAGS) -o $@ $^ $(LUAINC) $(LUALIB)

clean :
	rm -rf testlua.exe
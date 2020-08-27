all:
	clang -DDEBUG -Wall -Wextra -Isrc -Isrc/sdl2 src/sdl2/main.c src/01_parse_file.c $(shell sdl2-config --cflags) -o 01_parse_file.app  $(shell sdl2-config --libs)
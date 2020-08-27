

CC=clang
CFLAGS=-Wall -Wextra -O0 -g -fsanitize=address -Isrc -Isrc/sdl2 $(shell sdl2-config --cflags) 
LIBS=$(shell sdl2-config --libs) -lavformat
PLSRC=src/sdl2/*.c

all: 01_raw_video_play.app 02_pcm_audio_play.app 03_parse_file.app
	

01_raw_video_play.app:
	$(CC) $(CFLAGS) $(LIBS) $(PLSRC) src/01_raw_video_play.c -o $@


02_pcm_audio_play.app:
	$(CC) $(CFLAGS) $(LIBS) $(PLSRC) src/02_pcm_audio_play.c -o $@


03_parse_file.app:
	$(CC) $(CFLAGS) $(LIBS) $(PLSRC) src/03_parse_file.c -o $@

clean:
	rm -rf *.app
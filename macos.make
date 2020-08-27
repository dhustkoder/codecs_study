

CC=clang
CFLAGS=-Wall -Wextra -Isrc -Isrc/sdl2 $(shell sdl2-config --cflags) $(shell sdl2-config --libs)
PLSRC=src/sdl2/*.c


01_raw_video_play.app:
	$(CC) $(CFLAGS) $(PLSRC) src/01_raw_video_play.c -o $@


02_pcm_audio_play.app:
	$(CC) $(CFLAGS) $(PLSRC) src/02_pcm_audio_play.c -o $@


clean:
	rm -rf *.app
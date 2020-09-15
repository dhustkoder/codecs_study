

CFLAGS=-Wall -Wextra -O0 -fsanitize=address -Isrc -Isrc/sdl2 $(shell sdl2-config --cflags)
LIBS=$(shell sdl2-config --libs) -lavformat -lavcodec -lavutil -lswresample
PLSRC=src/sdl2/*.c


all: 01_raw_video_play.app 02_pcm_audio_play.app 03_libav_parse_file.app 04_libav_video_play.app 05_libav_audio_play.app 06_libav_video_audio_play.app
	
%.app: src/%.c 
	$(CC) $(CFLAGS) $(PLSRC) $^ $(LIBS) -o $(basename $(notdir $^)).app

clean:
	rm -rf *.app

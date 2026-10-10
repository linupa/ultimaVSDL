# Ultima V port. GAME = directory with the original game files.
#   src/    the game (ported code), the SDL front end
#   test/   headless tests and their scripted platform
#   build/  everything that is made: binaries, and what the tests write (screenshots, saves, logs)
GAME   ?= /home/linupa/srcs/ultima5
CC     ?= gcc
CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1
CPPFLAGS += -Isrc

SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS   ?= $(shell sdl2-config --libs)

B = build

GAME_SRC = $(addprefix src/,sound.c dungeon.c look.c shrine.c magic.c combat.c comsubs.c game.c cmds.c sjog.c outdoor.c world.c endings.c intro.c story.c picture.c shops.c status.c stats.c talk.c misc.c town.c npc.c objects.c view.c map.c tiles.c gfx.c lzw.c commands.c input.c text.c video.c files.c data.c)
GAME_HDR = $(wildcard src/*.h)

# Tests that run the game on the scripted platform.
SCRIPTED = test_view test_npc test_talk test_shops test_intro test_endings test_game test_video
TESTS    = test_data test_text $(SCRIPTED) test_sound

all: $(addprefix $(B)/,$(TESTS) u5sdl)

$(B):
	mkdir -p $(B)

$(B)/test_data: test/test_data.c src/data.c src/data.h | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test/test_data.c src/data.c

$(B)/test_text: test/test_text.c $(addprefix src/,text.c video.c files.c lzw.c data.c) $(GAME_HDR) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test/test_text.c $(addprefix src/,text.c video.c files.c lzw.c data.c)

$(addprefix $(B)/,$(SCRIPTED)): $(B)/%: test/%.c test/platform_script.c $(GAME_SRC) $(GAME_HDR) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< test/platform_script.c $(GAME_SRC)

$(B)/test_sound: test/test_sound.c $(GAME_SRC) $(GAME_HDR) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test/test_sound.c $(GAME_SRC)

$(B)/u5sdl: src/sdl_main.c src/sdl_platform.c $(GAME_SRC) $(GAME_HDR) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -o $@ src/sdl_main.c src/sdl_platform.c $(GAME_SRC) $(SDL_LIBS)

run: $(B)/u5sdl
	$(B)/u5sdl -w $(B)/u5save $(GAME)

# The tests run inside build/, so their screenshots and saves land there.
check: $(addprefix $(B)/,$(TESTS))
	cd $(B) && ./test_data $(GAME)/SAVED.GAM
	cd $(B) && ./test_text $(GAME)
	cd $(B) && ./test_view $(GAME)
	cd $(B) && ./test_npc $(GAME)
	cd $(B) && ./test_talk $(GAME)
	cd $(B) && ./test_shops $(GAME)
	cd $(B) && mkdir -p test_save && ./test_intro $(GAME) test_save > test_save/test_intro.log && echo "test_intro ok" || (grep FAIL test_save/test_intro.log; false)
	cd $(B) && ./test_endings $(GAME) test_save > test_save/test_endings.log && echo "test_endings ok" || (grep FAIL test_save/test_endings.log; false)
	cd $(B) && ./test_sound $(GAME)
	cd $(B) && for v in ega cga herc; do ./test_video $(GAME) test_save $$v > test_save/test_video_$$v.log && echo "test_video $$v ok" || (grep FAIL test_save/test_video_$$v.log; false) || exit 1; done
	cd $(B) && ./test_game $(GAME) test_save > test_save/test_game.log && echo "test_game ok" || (grep FAIL test_save/test_game.log; false)

clean:
	rm -rf $(B)

.PHONY: all run check clean

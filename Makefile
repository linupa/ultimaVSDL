# Ultima V port. GAME = directory with the original game files.
#   src/    the game (ported code), the SDL front end
#   test/   headless tests and their scripted platform
#   build/  everything that is made: binaries, and what the tests write (screenshots, saves, logs)
GAME   ?= /home/linupa/srcs/ultima5
CXX    ?= g++
CXXFLAGS ?= -std=c++17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1
# -iquote, not -I: src/strings.h must not stand in for the system <strings.h>.
CPPFLAGS += -iquote src

SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS   ?= $(shell sdl2-config --libs)

B = build

GAME_SRC = $(addprefix src/,sound.cpp dungeon.cpp look.cpp shrine.cpp magic.cpp combat.cpp comsubs.cpp game.cpp cmds.cpp sjog.cpp outdoor.cpp world.cpp endings.cpp intro.cpp story.cpp picture.cpp pictures.cpp party.cpp shops.cpp status.cpp stats.cpp strings.cpp talk.cpp misc.cpp town.cpp npc.cpp objects.cpp view.cpp map.cpp tiles.cpp gfx.cpp gfx_state.cpp lzw.cpp savegame.cpp gamedata.cpp gametime.cpp items.cpp monsters.cpp spells.cpp map_places.cpp commands.cpp input.cpp text.cpp textbuf.cpp video.cpp files.cpp)
GAME_HDR = $(wildcard src/*.h test/*.h)

# Tests that run the game on the scripted platform.
SCRIPTED = test_view test_npc test_talk test_shops test_intro test_endings test_game test_video
TESTS    = test_data test_text $(SCRIPTED) test_sound

all: $(addprefix $(B)/,$(TESTS) u5sdl)

GAME_OBJ = $(patsubst src/%.cpp,$(B)/obj/%.o,$(GAME_SRC))

$(B) $(B)/obj:
	mkdir -p $@

# Every source is compiled once; the binaries differ in their main and their platform.
$(B)/obj/%.o: src/%.cpp $(GAME_HDR) | $(B)/obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(B)/obj/%.o: test/%.cpp $(GAME_HDR) | $(B)/obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(B)/obj/sdl_main.o $(B)/obj/sdl_platform.o: CXXFLAGS += $(SDL_CFLAGS)

$(B)/test_data: $(addprefix $(B)/obj/,test_data.o strings.o gamedata.o files.o lzw.o pictures.o map_places.o savegame.o party.o gametime.o gfx_state.o)
	$(CXX) -o $@ $^

$(B)/test_text: $(addprefix $(B)/obj/,test_text.o text.o video.o files.o lzw.o strings.o gamedata.o pictures.o gfx_state.o)
	$(CXX) -o $@ $^

$(addprefix $(B)/,$(SCRIPTED)): $(B)/%: $(B)/obj/%.o $(B)/obj/platform_script.o $(GAME_OBJ)
	$(CXX) -o $@ $^

$(B)/test_sound: $(B)/obj/test_sound.o $(GAME_OBJ)
	$(CXX) -o $@ $^

$(B)/u5sdl: $(B)/obj/sdl_main.o $(B)/obj/sdl_platform.o $(GAME_OBJ)
	$(CXX) -o $@ $^ $(SDL_LIBS)

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

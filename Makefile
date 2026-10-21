# Commander Keen 1 for Linux (SDL2)
#
#   make            build ./keen1
#   make dist       build and package keen1-linux-x86_64.tar.gz with the game data
#   make web        browser build in build/web/ (Emscripten; needs emcc on PATH,
#                   e.g. source ~/tools/emsdk/emsdk_env.sh)
#
# SDL2 is found with sdl2-config / pkg-config. Without development packages,
# point SDL2_DIR at a directory containing include/SDL2/*.h and lib/libSDL2.so.

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
GAMEZIP ?= Commander Keen 1 Marooned On Mars (1990)(Apogee Software).zip

ifdef SDL2_DIR
  SDL_CFLAGS := -I$(SDL2_DIR)/include
  SDL_LIBS   := -L$(SDL2_DIR)/lib -Wl,-rpath-link,$(SDL2_DIR)/lib -lSDL2
else
  SDL_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2)
  SDL_LIBS   = $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2)
endif

keen1: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(SDL_LIBS) -lm

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

build:
	mkdir -p build

DIST := keen1-linux-x86_64
dist: keen1
	rm -rf $(DIST) $(DIST).tar.gz
	mkdir -p $(DIST)/data
	cp keen1 $(DIST)/
	# Game data: the clean v1.31 shareware files when extracted (tools/unshr.py),
	# otherwise the bundled zip; saves, scores and key settings come from the zip.
	unzip -q -o "$(GAMEZIP)" -d $(DIST)/data
	if [ -d shareware/data ]; then \
		for f in shareware/data/*; do \
			rm -f "$(DIST)/data/$$(basename "$$f" | tr A-Z a-z)"; cp "$$f" $(DIST)/data/; \
		done; \
	fi
	cp README.md $(DIST)/
	cp packaging/keen1.sh $(DIST)/run.sh
	chmod +x $(DIST)/run.sh
	tar czf $(DIST).tar.gz $(DIST)

# Browser build. The blocking DOS-style loops stay as they are: ASYNCIFY
# lets plat_vsync() yield to the browser. Only the shareware episode is
# bundled (WEB_DATA, preloaded read-only as /data).
EMCC     ?= emcc
WEB_DATA ?= shareware/data
WEB_OUT  := build/web
WEB_FLAGS := -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -sUSE_SDL=2 \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1MB -sALLOW_MEMORY_GROWTH \
	-sINVOKE_RUN=0 -sEXIT_RUNTIME -sEXPORTED_RUNTIME_METHODS=callMain -lidbfs.js \
	--pre-js web/pre.js --shell-file web/shell.html

web: $(WEB_OUT)/index.html

$(WEB_OUT)/index.html: $(SRC) src/*.h web/pre.js web/shell.html $(wildcard $(WEB_DATA)/*)
	@test -f $(WEB_DATA)/EGAHEAD.CK1 || { echo "No Keen 1 shareware data in $(WEB_DATA)" \
		"(extract 1keen.zip with tools/unshr.py or set WEB_DATA=dir)"; exit 1; }
	mkdir -p $(WEB_OUT)
	$(EMCC) $(WEB_FLAGS) -o $@ $(SRC) --preload-file $(WEB_DATA)@/data

# Headless WebAssembly build for Node: runs replays (--replay) so their
# checksums can be compared with the native build's. Real files via NODERAWFS.
NODE_OUT := build/node
NODE_FLAGS := -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -DKEEN_NODE -sUSE_SDL=2 \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1MB -sALLOW_MEMORY_GROWTH \
	-sENVIRONMENT=node -sNODERAWFS -sEXIT_RUNTIME

node: $(NODE_OUT)/keen1.js

$(NODE_OUT)/keen1.js: $(SRC) src/*.h
	mkdir -p $(NODE_OUT)
	$(EMCC) $(NODE_FLAGS) -o $@ $(SRC)

# Determinism tests: snapshot/restore self-test, real-time independence and
# (when the Node build exists) native-vs-wasm checksums. Needs shareware/data.
test: keen1
	tests/run.sh

clean:
	rm -rf build keen1 $(DIST) $(DIST).tar.gz

.PHONY: dist web node test clean

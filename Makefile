# Commander Keen 1 for Linux (SDL2)
#
#   make            build ./keen1
#   make dist       build and package keen1-linux-x86_64.tar.gz with the game data
#   make web        browser build in build/web/ (Emscripten; needs emcc on PATH,
#                   e.g. source ~/tools/emsdk/emsdk_env.sh)
#   make release    package the online server and web build as keen1-online-<version>.tar.gz
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
# lets plat_vsync() yield to the browser. No game data is bundled: web/pre.js
# fetches the shareware files from the server into /data when the page loads.
EMCC     ?= emcc
WEB_OUT  := build/web
WEB_FLAGS := -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -sUSE_SDL=2 \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1MB -sALLOW_MEMORY_GROWTH \
	-sINVOKE_RUN=0 -sEXIT_RUNTIME -sEXPORTED_RUNTIME_METHODS=callMain -lidbfs.js \
	--pre-js web/pre.js --pre-js web/net.js --shell-file web/shell.html

# The build id (a hash of the wasm file) goes into the page and next to it as
# build-id.txt, where the server reads it: a browser left open across an upgrade is
# told to reload.
web: $(WEB_OUT)/index.html

$(WEB_OUT)/index.html: $(SRC) src/*.h web/pre.js web/net.js web/shell.html
	mkdir -p $(WEB_OUT)
	$(EMCC) $(WEB_FLAGS) -o $@ $(SRC)
	@id=$$(sha256sum $(WEB_OUT)/index.wasm | cut -c1-16); \
	echo $$id > $(WEB_OUT)/build-id.txt; \
	sed -i "s/@BUILD_ID@/$$id/" $(WEB_OUT)/index.html

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

# Headless WebAssembly build for Node that plays online through web/net.js (a test client for
# tests/online): `make nodenet`, then see tests/online/README.md.
NODENET_OUT := build/nodenet
NODENET_FLAGS := $(NODE_FLAGS) -DKEEN_NODE_NET -sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=callMain \
	-sMODULARIZE -sEXPORT_NAME=createKeen --pre-js web/net.js

nodenet: $(NODENET_OUT)/keen1.js

$(NODENET_OUT)/keen1.js: $(SRC) src/*.h web/net.js
	mkdir -p $(NODENET_OUT)
	$(EMCC) $(NODENET_FLAGS) -o $@ $(SRC)

# Determinism tests: snapshot/restore self-test, real-time independence and
# (when the Node build exists) native-vs-wasm checksums. Needs shareware/data.
test: keen1
	tests/run.sh

# End-to-end online tests: the real server and headless game clients (needs make nodenet and
# `npm run build` in server/).
test-online: nodenet
	node --test tests/online/e2e.test.mjs

# Release package of the online game: the built server, the web build, the example config, the
# installer, the data unpacker (tools/unshr.py) and the README. It holds no game data: the installer
# fetches the shareware files, and the check below fails the build if any got in. It runs with
# Node 22 and `npm ci --omit=dev` in server/, no build step. The version is the one in
# server/package.json. The tree must be clean, so a release always matches a commit. Needs emcc
# on PATH (see `make web`). Releases are published by .github/workflows/release.yml from a tag.
VERSION := $(shell sed -n 's/^ *"version": *"\([^"]*\)".*/\1/p' server/package.json | head -n1)
RELEASE := keen1-online-$(VERSION)
RELEASE_DIR := build/release

release:
	@test -z "$$(git status --porcelain)" || { \
		echo "make release: uncommitted changes; commit or stash them first"; git status --short; exit 1; }
	$(MAKE) web
	cd server && npm ci && npm run build
	rm -rf $(RELEASE_DIR) $(RELEASE).tar.gz
	mkdir -p $(RELEASE_DIR)/$(RELEASE)/server
	cp -r server/dist server/package.json server/package-lock.json $(RELEASE_DIR)/$(RELEASE)/server/
	cp -r $(WEB_OUT) $(RELEASE_DIR)/$(RELEASE)/web
	cp -r deploy $(RELEASE_DIR)/$(RELEASE)/deploy
	mkdir -p $(RELEASE_DIR)/$(RELEASE)/tools
	cp tools/unshr.py $(RELEASE_DIR)/$(RELEASE)/tools/
	cp server/config.example.json .nvmrc README.md $(RELEASE_DIR)/$(RELEASE)/
	git rev-parse HEAD > $(RELEASE_DIR)/$(RELEASE)/COMMIT
	tar czf $(RELEASE).tar.gz -C $(RELEASE_DIR) $(RELEASE)
	@if tar -tzf $(RELEASE).tar.gz | grep -iE '\.(ck1|shr)$$|keen1\.exe$$|index\.data$$'; then \
		echo "make release: the package contains game data (listed above)"; rm -f $(RELEASE).tar.gz; exit 1; fi
	@echo "built $(RELEASE).tar.gz from $$(git rev-parse --short HEAD)"

clean:
	rm -rf build keen1 $(DIST) $(DIST).tar.gz keen1-online-*.tar.gz

.PHONY: dist web node nodenet test test-online release clean

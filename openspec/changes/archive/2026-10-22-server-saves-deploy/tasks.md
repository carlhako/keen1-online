# Tasks

Work in `/home/carl/projects/keen1-online` after `online-lobby-netplay` is complete. Server tests
run with `npm test` in `server/`.

## 1. Server saves

- [x] 1.1 Add the save store (one JSON file per save under the configured data directory, list loaded at start-up, newest first) with record validation (92 bytes, done flags and items 0/1, keys 0/1, sane ranges); verify tests for store, list order, restart reload, and refused malformed records
- [x] 1.2 Add `save` and `saves` protocol messages and the save list in the lobby (name, date, levels done, score); verify a test round trip and the lobby showing a stored save
- [x] 1.3 In the online build, make F5 on the map emit `PAUSE_ON`(saving), run the name prompt locally (20 characters, default name), send the save, show "Saved" or "Save failed", and emit `PAUSE_OFF`; verify in two tabs that the partner sees "PLAYER n IS SAVING" and that F5 in a level does nothing
- [x] 1.4 Add `load{saveId}` creating a game from a save (counted toward `maxGames`), with the record in the `start` event and the world map starting from it; verify loading a save made in another session restores score, items, completed levels and map position, and that loading at the limit is refused
- [x] 1.5 Document saving, loading and where saves live on disk in `README.md`; verify against a two-tab session

## 2. Server high scores

- [x] 2.1 Add the high score store (7 entries, original defaults, atomic writes) and include the shared score in checksum reports; verify tests for defaults, insertion order, atomic write and restart reload
- [x] 2.2 Accept submissions only after `finished`, with score not above the last reported score and a printable name up to 12 characters; verify tests for each refusal
- [x] 2.3 After the ending, run the original name entry for player 1 (or the only player) when the score qualifies, show "PLAYER 1 IS ENTERING YOUR TEAM NAME" to the others, then show the table to everyone before the lobby; verify with a replay or save starting just before the last ship part in two tabs
- [x] 2.4 Show the table in the lobby; verify it updates after a new entry
- [x] 2.5 Document the shared table in `README.md`; verify it matches the behaviour

## 3. Release and CI

- [x] 3.1 Add `.nvmrc` (22) and `make release` producing `keen1-online-<version>.tar.gz` (refusing with uncommitted changes); verify extracting it, `npm ci --omit=dev` in `server/` and `npm start` serve a playable lobby
- [x] 3.2 Add `.github/workflows/ci.yml` running server lint, typecheck and tests, the native build and the replay determinism test; verify the workflow passes on a push
- [ ] 3.3 Document releasing (`make release`, `gh release create`) in `README.md`; verify by publishing the first release

## 4. Installer and service

- [x] 4.1 Adapt `deploy/install.sh` from `locomotive-wars` (user, Node 22, releases under `/opt/keen1-online`, config written once to `/etc/keen1-online/config.json`, optional `/etc/keen1-online/env`, data in `/var/lib/keen1-online`, systemd unit, health check of `/` and `/ws`, rollback, `RELEASE_TAG` / `KO_TARBALL` / `KO_FORCE`); verify a fresh install in a test Debian LXC (or container) serves the lobby
- [x] 4.2 Verify upgrade paths in the test LXC: re-run with nothing new (no restart), upgrade to a newer release, and a deliberately broken release rolls back; saves and scores unchanged throughout
- [ ] 4.3 Document install, upgrade, config keys, data paths, and nginx and Caddy reverse-proxy settings (WebSocket upgrade on `/ws`, idle timeout) in `README.md`; verify the documented commands run as written in the test LXC

## 5. Integration check

- [x] 5.1 Install a release on the Proxmox LXC behind the reverse proxy, then from two browsers over `https`: play co-op, save on the map, load the save in a new game, and finish a game into the high score table; verify every step works through `wss://`

## Workflow follow-up

- Archive the four changes in order (`multiplayer-engine-core`, `coop-game-rules`, `online-lobby-netplay`, `server-saves-deploy`) as each is completed and reviewed.

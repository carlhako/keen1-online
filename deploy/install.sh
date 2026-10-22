#!/usr/bin/env bash
# Keen 1 online installer for a Debian (or Debian-based) server or LXC container.
#
# Creates the service user, installs Node.js from nodejs.org (the major version the release's
# .nvmrc names; Debian's own nodejs is too old), downloads the latest release from GitHub,
# installs its production dependencies, writes a config on first install, and installs and
# enables keen1-online.service so the game starts on boot.
#
# Re-running it upgrades in place: a newer release or Node.js patch is installed beside the
# running one and swapped in; if the new version does not come up, the previous one is restored.
# The config, the saves and the high score table are left alone. Nothing is restarted when
# nothing changed.
#
# The release carries no game data. The installer puts the Keen 1 v1.31 shareware files in
# /var/lib/keen1-online/game (kept across upgrades, skipped when already there and correct). Each
# file is checked against the md5 list below, so the zip may be packaged in any way. It takes the
# zip from KEEN_DATA_ZIP, else a 1keen.zip / keen1.zip / Commander_Keen_1_-_Marooned_on_Mars_1990.zip
# in the current directory or /root, else KEEN_DATA_URL, else the Internet Archive, else
# commander-keen.com (Apogee's installer, unpacked with tools/unshr.py).
#
# Usage (as root; a fresh Debian LXC lacks curl, so `apt-get install -y curl` first):
#   curl -fsSL https://raw.githubusercontent.com/carlhako/keen1-online/main/deploy/install.sh | bash
#
# Options, as environment variables:
#   RELEASE_TAG=v0.2.0                  install that release instead of the latest (also downgrades)
#   KO_TARBALL=/path/to/keen1-online-X.Y.Z.tar.gz   install a local release package
#   KO_FORCE=1                          reinstall and restart even when already up to date
#   KO_REPO=owner/name                  take releases from another GitHub repository
#   KEEN_DATA_ZIP=/path/to/1keen.zip    take the game files from this zip (an error if it is wrong)
#   KEEN_DATA_URL=https://.../1keen.zip download the zip from here instead (an error if it fails)
#   KO_IA_URL, KO_APOGEE_URL            replace the two built-in download addresses (for testing)
#
# Layout:
#   /opt/keen1-online/current -> releases/<version>     the game (root-owned, read-only to it)
#   /opt/keen1-online/node/current -> v<node version>   Node.js
#   /etc/keen1-online/config.json                       config (see the README for the keys)
#   /etc/keen1-online/env                               optional environment, e.g. PORT=8080
#   /var/lib/keen1-online/saves, highscores.json        the saved games and the high score table
#   /var/lib/keen1-online/game                          the Keen 1 shareware files (root-owned)

set -euo pipefail

REPO="${KO_REPO:-carlhako/keen1-online}"
RELEASE_TAG="${RELEASE_TAG:-latest}"
SERVICE=keen1-online
SERVICE_USER=keen1-online
PREFIX=/opt/keen1-online
CONF_DIR=/etc/keen1-online
CONFIG="$CONF_DIR/config.json"
STATE_DIR=/var/lib/keen1-online
GAME_DIR="$STATE_DIR/game"
IA_URL="${KO_IA_URL:-https://archive.org/download/msdos_Commander_Keen_1_-_Marooned_on_Mars_1990/Commander_Keen_1_-_Marooned_on_Mars_1990.zip}"
APOGEE_URL="${KO_APOGEE_URL:-https://www.commander-keen.com/downloads/keen1.zip}"
UNIT="/etc/systemd/system/$SERVICE.service"

log()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!! \033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mXX \033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run as root"
command -v apt-get >/dev/null || die "this installer needs a Debian-based system (apt-get not found)"
command -v systemctl >/dev/null || die "this installer needs systemd (systemctl not found)"
[[ "$REPO" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || die "KO_REPO must look like owner/name"

case "$(uname -m)" in
  x86_64|amd64)  node_arch=x64 ;;
  aarch64|arm64) node_arch=arm64 ;;
  armv7l)        node_arch=armv7l ;;
  *) die "unsupported architecture $(uname -m)" ;;
esac

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

# --- packages ----------------------------------------------------------------
missing=()
for cmd in curl tar gzip sha256sum; do command -v "$cmd" >/dev/null || missing+=("$cmd"); done
[ -e /etc/ssl/certs/ca-certificates.crt ] || missing+=(ca-certificates)
if [ "${#missing[@]}" -gt 0 ]; then
  log "installing curl, ca-certificates, tar, gzip"
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq curl ca-certificates tar gzip coreutils >/dev/null
fi

# --- service user --------------------------------------------------------------
if ! id "$SERVICE_USER" >/dev/null 2>&1; then
  log "creating service user '$SERVICE_USER'"
  useradd --system --user-group --home-dir "$STATE_DIR" --shell /usr/sbin/nologin "$SERVICE_USER"
fi

# --- which release -------------------------------------------------------------
version_of() { sed -n 's/^ *"version": *"\([^"]*\)".*/\1/p' "$1" 2>/dev/null | head -n1; }

installed_version=""
[ -f "$PREFIX/current/server/package.json" ] && installed_version="$(version_of "$PREFIX/current/server/package.json")"

if [ -n "${KO_TARBALL:-}" ]; then
  [ -f "$KO_TARBALL" ] || die "KO_TARBALL: no such file $KO_TARBALL"
  tarball="$KO_TARBALL"
  top="$(tar -tzf "$tarball" | sed -n '1s|/.*||p')"
  target_version="${top#keen1-online-}"
else
  if [ "$RELEASE_TAG" = "latest" ]; then
    # The latest-release page redirects to .../releases/tag/<tag>; no API call or JSON parser needed.
    tag="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest")" \
      || die "could not reach GitHub to find the latest release"
    tag="${tag##*/}"
    [[ "$tag" =~ ^v[0-9] ]] || die "could not find the latest release of $REPO"
  else
    tag="v${RELEASE_TAG#v}"
  fi
  target_version="${tag#v}"
  tarball=""
fi
[[ "$target_version" =~ ^[0-9A-Za-z.+-]+$ ]] || die "unexpected release version '$target_version'"

app_changed=0
if [ "$installed_version" != "$target_version" ] || [ -n "${KO_FORCE:-}" ]; then
  app_changed=1
fi

if [ "$app_changed" -eq 1 ]; then
  if [ -z "$tarball" ]; then
    url="https://github.com/$REPO/releases/download/$tag/keen1-online-$target_version.tar.gz"
    log "downloading Keen 1 online $target_version"
    curl -fsSL "$url" -o "$work/release.tar.gz" || die "could not download $url"
    tarball="$work/release.tar.gz"
  fi
  tar -xzf "$tarball" -C "$work" --no-same-owner
  src="$work/keen1-online-$target_version"
  [ -f "$src/server/dist/main.js" ] || die "release package has no built server"
  nvmrc="$src/.nvmrc"
else
  log "Keen 1 online $installed_version is the latest release"
  nvmrc="$PREFIX/current/.nvmrc"
fi

# --- Node.js -------------------------------------------------------------------
node_major="$(sed -nE '1s/^v?([0-9]+).*/\1/p' "$nvmrc" 2>/dev/null || true)"
node_major="${node_major:-22}"
old_node="$(readlink "$PREFIX/node/current" 2>/dev/null || true)"
node_dir="$old_node"

if curl -fsSL "https://nodejs.org/dist/latest-v${node_major}.x/SHASUMS256.txt" -o "$work/SHASUMS256.txt"; then
  node_file="$(grep -oE "node-v[0-9.]+-linux-${node_arch}\.tar\.gz" "$work/SHASUMS256.txt" | head -n1)"
  [ -n "$node_file" ] || die "nodejs.org has no Node.js $node_major build for linux-$node_arch"
  node_version="$(echo "$node_file" | sed -E 's/^node-(v[0-9.]+)-.*/\1/')"
  node_dir="$node_version"
  if [ ! -x "$PREFIX/node/$node_version/bin/node" ]; then
    log "installing Node.js $node_version"
    curl -fsSL "https://nodejs.org/dist/$node_version/$node_file" -o "$work/$node_file" \
      || die "could not download Node.js $node_version"
    (cd "$work" && grep " $node_file\$" SHASUMS256.txt | sha256sum -c --quiet -) \
      || die "Node.js download failed its checksum"
    mkdir -p "$PREFIX/node"
    rm -rf "$PREFIX/node/$node_version.new"
    mkdir "$PREFIX/node/$node_version.new"
    tar -xzf "$work/$node_file" -C "$PREFIX/node/$node_version.new" --strip-components=1 --no-same-owner
    mv "$PREFIX/node/$node_version.new" "$PREFIX/node/$node_version"
  fi
elif [ -n "$old_node" ]; then
  warn "could not reach nodejs.org; keeping Node.js $old_node"
else
  die "could not reach nodejs.org to download Node.js $node_major"
fi
node_bin="$PREFIX/node/$node_dir/bin"
node_changed=0
[ "$node_dir" != "$old_node" ] && node_changed=1

# --- the game data ---------------------------------------------------------------
# md5 of each Keen 1 v1.31 shareware file the page needs (server/src/gamefiles.ts names the same
# files; a test keeps the two lists equal). CTLPANEL.CK1 and SCORES.CK1 are written by the game.
game_md5s() {
  cat <<'LIST'
b1e632ee0483b66c2f1dff487547753b  EGAHEAD.CK1
80a5870d9b48970333837fa0293561ed  EGALATCH.CK1
ed9a0d1736034c155555543263939fe7  EGASPRIT.CK1
3d9b5805e21a7cc7b248664b0b8f6494  ENDTEXT.CK1
a44bcb798f931dc392903dcc5446c068  FINALE.CK1
476301a5a6ac41e39ea18d900e1c8e02  HELPTEXT.CK1
8b2b1f335bb49e0b81ad66af582de2ef  KEEN1.EXE
075a98e05b6d5bbed80ec21420577582  LEVEL01.CK1
2d6b634fbce4aa357375d62300fb3e07  LEVEL02.CK1
956f2bbf2ad4fe7c52ad90962bd92166  LEVEL03.CK1
57b24d48be8748362a0c5e2277d2d859  LEVEL04.CK1
1b6cd04bffb03fe26b722260da3316f9  LEVEL05.CK1
7859497ced955aa87b4a935b43ae15b8  LEVEL06.CK1
337259268ca2338ee4d16818cbcd59d4  LEVEL07.CK1
51b099927527e88f8450163d05c2ca39  LEVEL08.CK1
de0e0de6b46474e2f4f06fc9abfd3e61  LEVEL09.CK1
0bb9ac865cdcbd46bdcacd493669d499  LEVEL10.CK1
b341dd49e84d7580a9eb4280827a7251  LEVEL11.CK1
edf1eb3db5a424360996e82ecf1dba93  LEVEL12.CK1
0cb4b321cdbb2d69682c258788ce8e4b  LEVEL13.CK1
5d6d6814d7e7bf91dde2ec1792d45976  LEVEL14.CK1
09b0dc88bdb234d6b2efd5b8ac973dc0  LEVEL15.CK1
6ee167a36e7517320ff7ae6138f22e9d  LEVEL16.CK1
bfe6781e44056cbc248701ae192122ac  LEVEL80.CK1
b86086e33b4fb169f96abdf002fd64e3  LEVEL81.CK1
dd1a09d6e332e0ae13c78670763ed891  LEVEL90.CK1
c8c4b2ce09a1d19d2763493c7e7ad44a  PREVIEW2.CK1
eaff03166eba1d1d5da749b0d687b3a6  PREVIEW3.CK1
c480310fb66b36854b293264dbf6d8ff  PREVIEWS.CK1
c726f4948d49c5b212cc6f6bc8a72247  SOUNDS.CK1
0f6e2c7fc0ec2df428757386b072b071  STORYTXT.CK1
LIST
}

# Whether every game file is in $1 with the right md5; the first bad one goes in $bad_file.
game_files_ok() {
  local dir="$1" sum name
  bad_file=""
  while read -r sum name; do
    if [ ! -f "$dir/$name" ] || [ "$(md5sum < "$dir/$name" | cut -d' ' -f1)" != "$sum" ]; then
      bad_file="$name"; return 1
    fi
  done < <(game_md5s)
}

apt_install() {
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq "$@" >/dev/null
}

# Takes the game files out of the zip $1 into $work/new, checking each one. Works for a zip of
# plain files in any folder, and for Apogee's installer (CK1SW131.SHR inside). Returns 1 with a
# message when the zip is not usable; nothing outside $work is touched.
extract_game_data() {
  local zip="$1" tree="$work/zip" shr sum name hit
  rm -rf "$tree" "$work/new"; mkdir -p "$tree" "$work/new"
  command -v unzip >/dev/null || apt_install unzip || { warn "could not install unzip"; return 1; }
  unzip -q -o "$zip" -d "$tree" 2>/dev/null || { warn "$zip is not a usable zip file"; return 1; }
  shr="$(find "$tree" -type f -iname 'CK1SW131.SHR' | head -n1)"
  if [ -n "$shr" ]; then
    local unshr="${tools_src:+$tools_src/tools/unshr.py}"
    [ -f "${unshr:-}" ] || { warn "this release has no tools/unshr.py to unpack $zip"; return 1; }
    command -v python3 >/dev/null || apt_install python3 || { warn "could not install python3"; return 1; }
    mkdir -p "$tree/_shr"
    python3 "$unshr" "$shr" "$tree/_shr" >/dev/null 2>&1 || { warn "could not unpack CK1SW131.SHR from $zip"; return 1; }
  fi
  while read -r sum name; do
    hit=""
    while IFS= read -r f; do
      if [ "$(md5sum < "$f" | cut -d' ' -f1)" = "$sum" ]; then hit="$f"; break; fi
    done < <(find "$tree" -type f -iname "$name")
    if [ -z "$hit" ]; then
      if [ -n "$(find "$tree" -type f -iname "$name" | head -n1)" ]; then
        warn "$name in $zip is not the Keen 1 v1.31 file (its md5 differs)"
      else
        warn "$name is not in $zip"
      fi
      return 1
    fi
    cp "$hit" "$work/new/$name"
  done < <(game_md5s)
}

# Looks for the game files as described at the top. A source the operator named (KEEN_DATA_ZIP,
# KEEN_DATA_URL) must work; the others are tried in turn.
get_game_data() {
  local zip name dir url
  if [ -n "${KEEN_DATA_ZIP:-}" ]; then
    [ -f "$KEEN_DATA_ZIP" ] || die "KEEN_DATA_ZIP: no such file $KEEN_DATA_ZIP"
    log "reading the game data from $KEEN_DATA_ZIP"
    extract_game_data "$KEEN_DATA_ZIP" || die "the game data in $KEEN_DATA_ZIP was rejected (nothing was installed)"
    return 0
  fi
  for dir in "$PWD" /root; do
    for name in 1keen.zip keen1.zip Commander_Keen_1_-_Marooned_on_Mars_1990.zip; do
      zip="$dir/$name"
      [ -f "$zip" ] || continue
      log "trying the game data in $zip"
      extract_game_data "$zip" && return 0
    done
  done
  if [ -n "${KEEN_DATA_URL:-}" ]; then
    log "downloading the game data from $KEEN_DATA_URL"
    curl -fsSL --retry 3 --retry-all-errors "$KEEN_DATA_URL" -o "$work/data.zip" \
      || die "could not download $KEEN_DATA_URL"
    extract_game_data "$work/data.zip" || die "the game data from $KEEN_DATA_URL was rejected (nothing was installed)"
    return 0
  fi
  for url in "$IA_URL" "$APOGEE_URL"; do
    log "downloading the game data from $url"
    if curl -fsSL --retry 3 --retry-all-errors "$url" -o "$work/data.zip"; then
      extract_game_data "$work/data.zip" && return 0
    else
      warn "could not download $url"
    fi
  done
  return 1
}

install -d -m 0750 -o "$SERVICE_USER" -g "$SERVICE_USER" "$STATE_DIR"
if game_files_ok "$GAME_DIR"; then
  log "the Keen 1 game data is already installed"
else
  tools_src="${src:-$PREFIX/current}"
  get_game_data || die "could not get the Keen 1 shareware files. Download 1keen.zip (from archive.org or
   commander-keen.com), copy it to /root on this machine and run the installer again; or set
   KEEN_DATA_ZIP=/path/to/zip, or KEEN_DATA_URL=https://... (see the README)"
  log "installing the game data to $GAME_DIR"
  rm -rf "$GAME_DIR.new" "$GAME_DIR.old"
  mv "$work/new" "$GAME_DIR.new"
  chown -R root:root "$GAME_DIR.new"
  chmod 0755 "$GAME_DIR.new"; chmod 0644 "$GAME_DIR.new"/*
  [ -d "$GAME_DIR" ] && mv "$GAME_DIR" "$GAME_DIR.old"
  mv "$GAME_DIR.new" "$GAME_DIR"
  rm -rf "$GAME_DIR.old"
fi

# --- the game ------------------------------------------------------------------
old_app="$(readlink "$PREFIX/current" 2>/dev/null || true)"
new_app="$old_app"
if [ "$app_changed" -eq 1 ]; then
  new_app="releases/$target_version"
  dest="$PREFIX/$new_app"
  if [ "$new_app" = "$old_app" ]; then
    # Forced reinstall of the running version: build beside it and swap in after the stop.
    dest="$PREFIX/releases/$target_version.new"
  fi
  log "installing Keen 1 online $target_version to $PREFIX/releases/$target_version"
  mkdir -p "$PREFIX/releases"
  rm -rf "$dest"
  cp -a "$src" "$dest"
  chown -R root:root "$dest"
  (cd "$dest/server" && PATH="$node_bin:$PATH" npm ci --omit=dev --ignore-scripts --no-audit --no-fund --no-update-notifier \
      --loglevel=error --cache "$work/npm-cache") \
    || { rm -rf "$dest"; die "could not install the game's dependencies"; }
fi

# --- config --------------------------------------------------------------------
install -d -m 0750 -o root -g "$SERVICE_USER" "$CONF_DIR"
if [ ! -f "$CONFIG" ]; then
  log "writing $CONFIG"
  "$node_bin/node" -e '
    const fs = require("fs");
    const c = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
    c.dataDir = process.argv[2];
    fs.writeFileSync(process.argv[3], JSON.stringify(c, null, 2) + "\n");
  ' "$PREFIX/$new_app/config.example.json" "$STATE_DIR" "$CONFIG"
  chown root:"$SERVICE_USER" "$CONFIG"
  chmod 0640 "$CONFIG"
fi
install -d -m 0750 -o "$SERVICE_USER" -g "$SERVICE_USER" "$STATE_DIR"

# --- systemd unit --------------------------------------------------------------
cat > "$work/unit" <<EOF
[Unit]
Description=Keen 1 online game server
Documentation=https://github.com/$REPO
After=network-online.target
Wants=network-online.target

[Service]
Type=exec
User=$SERVICE_USER
Group=$SERVICE_USER
WorkingDirectory=$STATE_DIR
Environment=NODE_ENV=production
Environment=KEEN_CONFIG=$CONFIG
# Optional overrides, e.g. PORT=80.
EnvironmentFile=-$CONF_DIR/env
ExecStart=$PREFIX/node/current/bin/node $PREFIX/current/server/dist/main.js
Restart=on-failure
RestartSec=3
TimeoutStopSec=30

# Lets the server listen on a port below 1024 (such as 80) without running as root.
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE

# The server writes only its saves and its high score table.
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
StateDirectory=$SERVICE
ReadWritePaths=$STATE_DIR

[Install]
WantedBy=multi-user.target
EOF
unit_changed=0
cmp -s "$work/unit" "$UNIT" || unit_changed=1

# --- swap and start ------------------------------------------------------------
port() {
  local p
  p="$(sed -n 's/^PORT=//p' "$CONF_DIR/env" 2>/dev/null | tail -n1 | tr -d "\"' ")"
  [ -n "$p" ] || p="$("$node_bin/node" -e 'console.log(JSON.parse(require("fs").readFileSync(process.argv[1], "utf8")).port ?? 8080)' "$CONFIG")"
  echo "$p"
}

running=0
systemctl is-active --quiet "$SERVICE" && running=1

# Something else on the port (such as a Keen 1 online started by hand) would keep the service
# from listening, and could answer the health check in its place.
if [ "$running" -eq 0 ] && (exec 3<>"/dev/tcp/127.0.0.1/$(port)") 2>/dev/null; then
  die "port $(port) is already in use by another program; stop it (an older server started by hand?) and run this again"
fi

if [ "$app_changed$node_changed$unit_changed" = "000" ] && [ "$running" -eq 1 ]; then
  systemctl enable --quiet "$SERVICE"
  log "nothing to do; Keen 1 online $installed_version is running"
  exit 0
fi

# The server keeps the save list and the table in memory, so it is stopped before the swap.
if [ "$running" -eq 1 ]; then
  log "stopping $SERVICE"
  systemctl stop "$SERVICE"
fi
[ -f "$UNIT" ] && cp -a "$UNIT" "$work/unit.old"

if [ "$app_changed" -eq 1 ] && [ "$new_app" = "$old_app" ]; then
  rm -rf "$PREFIX/$old_app.old"
  mv "$PREFIX/$old_app" "$PREFIX/$old_app.old"
  mv "$PREFIX/$old_app.new" "$PREFIX/$old_app"
fi
ln -sfn "$new_app" "$PREFIX/current.new" && mv -T "$PREFIX/current.new" "$PREFIX/current"
ln -sfn "$node_dir" "$PREFIX/node/current.new" && mv -T "$PREFIX/node/current.new" "$PREFIX/node/current"
install -m 0644 "$work/unit" "$UNIT"
systemctl daemon-reload
systemctl enable --quiet "$SERVICE"

# /ws must accept a WebSocket upgrade, or the page would never get past "Connecting".
ws_upgrades() {
  "$node_bin/node" -e '
    const net = require("net");
    const s = net.connect(Number(process.argv[1]), "127.0.0.1");
    let head = "";
    const done = (ok) => { s.destroy(); process.exit(ok ? 0 : 1); };
    s.setTimeout(3000, () => done(false));
    s.on("error", () => done(false));
    s.on("connect", () => s.write(
      "GET /ws HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n" +
      "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n"));
    s.on("data", (d) => { head += d; if (head.includes("\r\n")) done(/^HTTP\/1\.1 101 /.test(head)); });
  ' "$(port)"
}

# The new server must answer /healthz (with this version, once the server reports one), serve the
# page at / and accept a WebSocket upgrade on /ws.
wait_healthy() {
  local base body i
  base="http://127.0.0.1:$(port)"
  for i in $(seq 1 30); do
    if body="$(curl -fsS --max-time 2 "$base/healthz" 2>/dev/null)" && [[ "$body" == *'"status":"ok"'* ]] \
      && [ "$(systemctl show -p SubState --value "$SERVICE")" = "running" ]; then
      if [[ "$body" != *'"version":'* ]] || [[ "$body" == *"\"version\":\"$target_version\""* ]]; then
        curl -fsS --max-time 2 -o /dev/null "$base/" 2>/dev/null && ws_upgrades && return 0
      fi
    fi
    [ "$(systemctl show -p ActiveState --value "$SERVICE")" = "failed" ] && return 1
    sleep 1
  done
  return 1
}

# Whether a server answers /healthz at all, for after a rollback (any version will do).
wait_up() {
  local i
  for i in $(seq 1 30); do
    curl -fsS --max-time 2 -o /dev/null "http://127.0.0.1:$(port)/healthz" 2>/dev/null && return 0
    sleep 1
  done
  return 1
}

log "starting Keen 1 online $target_version"
systemctl start "$SERVICE"
if ! wait_healthy; then
  if [ -n "$old_app" ] && [ -n "$old_node" ]; then
    warn "Keen 1 online $target_version did not start; restoring the previous version"
    systemctl stop "$SERVICE" 2>/dev/null || true
    if [ -d "$PREFIX/$old_app.old" ]; then
      rm -rf "$PREFIX/$old_app"; mv "$PREFIX/$old_app.old" "$PREFIX/$old_app"
    elif [ "$new_app" != "$old_app" ]; then
      rm -rf "${PREFIX:?}/$new_app"
    fi
    ln -sfn "$old_app" "$PREFIX/current"
    ln -sfn "$old_node" "$PREFIX/node/current"
    [ -f "$work/unit.old" ] && install -m 0644 "$work/unit.old" "$UNIT"
    systemctl daemon-reload
    systemctl start "$SERVICE"
    if wait_up; then
      die "upgrade failed; the previous version is running again (journalctl -u $SERVICE)"
    fi
    die "upgrade failed, and the previous version did not start either (journalctl -u $SERVICE)"
  fi
  die "Keen 1 online $target_version did not start (journalctl -u $SERVICE)"
fi

# --- tidy up: keep the running release and Node.js, and the ones before them ----------
rm -rf "$PREFIX/releases/"*.old "$PREFIX/releases/"*.new
for dir in "$PREFIX"/releases/*; do
  name="releases/$(basename "$dir")"
  [ "$name" = "$new_app" ] || [ "$name" = "$old_app" ] || rm -rf "$dir"
done
for dir in "$PREFIX"/node/v*; do
  [ -e "$dir" ] || continue
  name="$(basename "$dir")"
  [ "$name" = "$node_dir" ] || [ "$name" = "$old_node" ] || rm -rf "$dir"
done

log "done. Keen 1 online $target_version is running on Node.js $node_dir"
log "play at http://$(hostname -I 2>/dev/null | awk '{print $1}'):$(port)/"

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
# Usage (as root; a fresh Debian LXC lacks curl, so `apt-get install -y curl` first):
#   curl -fsSL https://raw.githubusercontent.com/carlhako/keen1-online/main/deploy/install.sh | bash
#
# Options, as environment variables:
#   RELEASE_TAG=v0.2.0                  install that release instead of the latest (also downgrades)
#   KO_TARBALL=/path/to/keen1-online-X.Y.Z.tar.gz   install a local release package
#   KO_FORCE=1                          reinstall and restart even when already up to date
#   KO_REPO=owner/name                  take releases from another GitHub repository
#
# Layout:
#   /opt/keen1-online/current -> releases/<version>     the game (root-owned, read-only to it)
#   /opt/keen1-online/node/current -> v<node version>   Node.js
#   /etc/keen1-online/config.json                       config (see the README for the keys)
#   /etc/keen1-online/env                               optional environment, e.g. PORT=8080
#   /var/lib/keen1-online/saves, highscores.json        the saved games and the high score table

set -euo pipefail

REPO="${KO_REPO:-carlhako/keen1-online}"
RELEASE_TAG="${RELEASE_TAG:-latest}"
SERVICE=keen1-online
SERVICE_USER=keen1-online
PREFIX=/opt/keen1-online
CONF_DIR=/etc/keen1-online
CONFIG="$CONF_DIR/config.json"
STATE_DIR=/var/lib/keen1-online
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

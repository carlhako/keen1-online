// Browser side of the online game, included with --pre-js (see the Makefile).
//
// It owns the WebSocket to the server, keeps the lobby state for the page (web/shell.html) and
// is the bridge the C game calls every frame (src/net.c). The messages are described in
// server/README.md. In short: the server seals one frame at a time once it has every seat's input
// for it; this file queues the sealed frames for the game, which simulates them in order.

Module.net = (function () {
  'use strict';

  var PROTOCOL = 1;
  var UP_CHECKPOINT = 1, UP_RESYNC = 2;
  var PING_MS = 2000;
  var FRAME_MS = 41;

  var net = {
    state: 'connecting',        // connecting | lobby | waiting | playing | lost | outdated
    cfg: null,                  // from welcome: checkpointFrames, inputLeadMax, disconnectSec
    lobby: null,                // the last lobby message
    game: null,                 // {id, seat, token, phase} once joined
    duty: false,                // this browser uploads checkpoints
    paused: null,               // seats that left, while the game waits for Continue
    rtt: 0,                     // round trip, ms (smoothed)
    onchange: function () {},   // the page redraws when this is called
    onboot: function () {},     // boot information arrived: the page may start the game
    onleave: function () {}     // the server sent us back to the lobby
  };

  var ws = null;
  var frames = {};              // sealed frames by number
  var head = -1;                // the highest sealed frame received
  var present = 0;              // seats with a Keen, as of `head`
  var boot = null;              // {f, fresh, seed, mask, snap, ready, taken}: where the game starts
  var pendingRestore = null;    // a restore that arrived while the game runs
  var snapWanted = false;
  var golive = -1;
  var rttSamples = [];
  var waitStart = 0;
  var lastSentPing = 0;

  // ---- rejoin tokens, kept in this browser ----

  var TOKENS = 'keen1.tokens';
  function readTokens() {
    try { return JSON.parse(localStorage.getItem(TOKENS)) || {}; } catch (e) { return {}; }
  }
  function writeTokens(t) {
    try { localStorage.setItem(TOKENS, JSON.stringify(t)); } catch (e) { /* private window */ }
  }
  // Tokens are kept per game and seat (two tabs of one browser share this storage and may hold both
  // seats of a game). The one to use is for a seat that is open: `open` lists those.
  net.tokenFor = function (id, open) {
    var list = readTokens()[id] || [];
    for (var i = 0; i < list.length; i++) if (open.indexOf(list[i].seat) >= 0) return list[i];
    return null;
  };

  // ---- connection ----

  function send(msg) {
    if (ws && ws.readyState === 1) ws.send(JSON.stringify(msg));
  }
  net.send = send;

  net.connect = function () {
    var proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(proto + '//' + location.host + '/ws');
    ws.binaryType = 'arraybuffer';
    ws.onopen = function () {
      send({ t: 'hello', v: PROTOCOL, build: window.KEEN_BUILD_ID });
    };
    ws.onmessage = function (e) {
      if (typeof e.data === 'string') message(JSON.parse(e.data));
      else binary(new Uint8Array(e.data));
    };
    ws.onclose = function () {
      if (net.state === 'outdated') return;
      var was = net.state;
      net.state = 'lost';
      net.onchange(was);
    };
    setInterval(function () {
      lastSentPing = performance.now();
      send({ t: 'ping', c: Math.round(lastSentPing) });
    }, PING_MS);
  };

  function message(m) {
    switch (m.t) {
    case 'welcome':
      net.cfg = m.cfg;
      net.state = 'lobby';
      lastSentPing = performance.now();       // measure the round trip right away
      send({ t: 'ping', c: Math.round(lastSentPing) });
      break;
    case 'reject':
      if (m.reason === 'version') net.state = 'outdated';
      net.rejected = m;
      setTimeout(function () {      // the message is for a moment, not for good
        if (net.rejected === m) {
          net.rejected = null;
          net.onchange();
        }
      }, 5000);
      break;
    case 'lobby':
      net.lobby = m;
      pruneTokens(m.games);
      break;
    case 'joined':
      net.rejected = null;
      net.game = { id: m.game, seat: m.seat, token: m.token, phase: m.phase };
      var t = readTokens();
      var list = (t[m.game] || []).filter(function (e) { return e.seat !== m.seat; });
      list.push({ token: m.token, seat: m.seat });
      t[m.game] = list;
      writeTokens(t);
      net.state = m.phase === 'waiting' ? 'waiting' : 'playing';
      break;
    case 'start':
      newBoot({ f: 0, fresh: true, seed: m.seed, mask: m.mask });
      boot.ready = true;
      net.state = 'playing';
      net.onboot();
      break;
    case 'restore':
      var info = { f: m.f, fresh: m.fresh, seed: m.seed, mask: m.mask, snap: null, ready: m.fresh, joiner: true };
      frames = {};
      head = m.f - 1;
      present = m.mask;
      if (!boot) {
        boot = info;
        net.state = 'playing';
        if (info.ready) net.onboot();
      } else {
        pendingRestore = info;
      }
      break;
    case 'frame':
      frames[m.n] = m;
      head = m.n;
      trackPresent(m);
      break;
    case 'golive':
      golive = m.from;
      break;
    case 'snap':
      snapWanted = true;
      break;
    case 'duty':
      net.duty = m.on;
      break;
    case 'paused':
      net.paused = m.seats;
      break;
    case 'resumed':
      net.paused = null;
      break;
    case 'ended':
      net.ended = true;
      break;
    case 'left':
      net.game = null;
      net.paused = null;
      net.state = 'lobby';
      net.onleave();
      break;
    case 'pong':
      rtt(performance.now() - m.c);
      return;
    }
    net.onchange();
  }

  function newBoot(info) {
    boot = info;
    boot.snap = null;
    frames = {};
    head = -1;
    present = info.mask;
  }

  // A snapshot for the restore message just before it.
  function binary(buf) {
    if (buf[0] !== 1 || buf.length < 5) return;
    var snap = buf.subarray(5);
    var target = pendingRestore || (boot && !boot.ready ? boot : null);
    if (!target) return;
    target.snap = snap;
    target.ready = true;
    if (target === boot) net.onboot();
  }

  function trackPresent(m) {
    for (var i = 0; m.ev && i < m.ev.length; i++) {
      var e = m.ev[i];
      if (e.t === 'start') present = e.mask;
      else if (e.t === 'add') present |= 1 << e.seat;
      else if (e.t === 'remove') present &= ~(1 << e.seat);
    }
  }

  function pruneTokens(games) {
    var t = readTokens(), live = {}, changed = false;
    for (var i = 0; i < games.length; i++) live[games[i].id] = true;
    for (var id in t) if (!live[id] && !(net.game && net.game.id == id)) { delete t[id]; changed = true; }
    if (changed) writeTokens(t);
  }

  // ---- round trip and the input lead ----

  function rtt(ms) {
    rttSamples.push(ms);
    if (rttSamples.length > 8) rttSamples.shift();
    var sorted = rttSamples.slice().sort(function (a, b) { return a - b; });
    net.rtt = sorted[sorted.length >> 1];
  }

  // Frames ahead to schedule our input: 2 normally, more when the round trip needs it.
  net.lead = function () {
    var max = net.cfg ? net.cfg.inputLeadMax : 6;
    var lead = Math.ceil(net.rtt / FRAME_MS) + 1;
    return Math.max(2, Math.min(max, lead));
  };

  // ---- lobby actions ----

  net.create = function () { send({ t: 'create' }); };
  net.join = function (id) {
    var games = net.lobby ? net.lobby.games : [];
    var open = [];
    for (var i = 0; i < games.length; i++) if (games[i].id === id) open = games[i].open;
    var t = net.tokenFor(id, open);
    send(t ? { t: 'join', game: id, token: t.token } : { t: 'join', game: id });
  };
  net.single = function () { send({ t: 'single' }); };
  net.leave = function () { send({ t: 'leave' }); };
  net.continueSolo = function () { send({ t: 'continue' }); };

  // ---- the game's side (src/net.c) ----

  net.bootReady = function () { return !!boot && boot.ready; };
  net.bootInfo = function () {
    boot.taken = true;
    return boot;
  };

  net.sendInput = function (f, bits) { send({ t: 'input', f: f, b: bits }); };

  // The sealed frame `f`, null when it has not arrived.
  net.frame = function (f) {
    var m = frames[f];
    if (m) {
      waitStart = 0;
      net.waiting = null;
      delete frames[f - 64];   // old frames are not needed again
    }
    return m || null;
  };

  net.backlog = function (next) { return head - next + 1; };

  // The game waits for frame `f`. After half a second the page says who for.
  net.wait = function (f) {
    var now = performance.now();
    if (!waitStart) waitStart = now;
    var shown = now - waitStart > 500;
    var who = net.game && ((present >> (1 - net.game.seat)) & 1) ? 'player ' + (2 - net.game.seat) : 'the server';
    var text = shown ? 'Waiting for ' + who : null;
    if (text !== net.waiting) {
      net.waiting = text;
      net.onchange();
    }
  };

  net.sum = function (f, c, loc) { send({ t: 'sum', f: f, c: c >>> 0, l: loc }); };

  net.upload = function (kind, f, sum, data) {
    if (!ws || ws.readyState !== 1) return;
    var out = new Uint8Array(9 + data.length);
    var dv = new DataView(out.buffer);
    out[0] = kind;
    dv.setUint32(1, f, true);
    dv.setUint32(5, sum >>> 0, true);
    out.set(data, 9);
    ws.send(out);
  };

  net.takeSnapRequest = function () {
    var w = snapWanted;
    snapWanted = false;
    return w;
  };

  // A restore that arrived mid-game (resync), or null.
  net.takeRestore = function () {
    if (!pendingRestore || !pendingRestore.ready) return null;
    var r = pendingRestore;
    pendingRestore = null;
    return r;
  };

  net.ready = function (f) { send({ t: 'ready', f: f }); };
  net.takeGolive = function () {
    var g = golive;
    golive = -1;
    return g;
  };

  // What the game told the server about the ending (for the console: Module.net.trace)
  net.trace = [];
  net.ending = function () { net.trace.push('ending ' + Math.round(performance.now())); send({ t: 'ending' }); };
  net.finished = function () { net.trace.push('finished ' + Math.round(performance.now())); send({ t: 'finished' }); };
  // Test aids, also usable from the console. The game runs a command the next time it polls (see
  // run_command in src/net.c): 1 corrupts its state, 2 moves a Keen, 3 warps (frame, player, x, y),
  // 4 sets the ship parts (frame, JBVW). Commands 3 and 4 act on a frame, so give every client the
  // same arguments and nobody drifts.
  var commands = [];
  net.testCommand = function (code, args) { commands.push([code, args || []]); };
  net.takeCommand = function () {
    var c = commands.shift();
    if (!c) return 0;
    net.commandArgs = c[1];
    return c[0];
  };

  // For debugging in the console: Module.net.debug()
  net.debug = function () {
    return { head: head, present: present, golive: golive, boot: boot && { f: boot.f, fresh: boot.fresh, ready: boot.ready, taken: boot.taken }, pending: !!pendingRestore, snapWanted: snapWanted };
  };
  net.UP_CHECKPOINT = UP_CHECKPOINT;
  net.UP_RESYNC = UP_RESYNC;

  return net;
})();

// Browser glue for the Emscripten build, included with --pre-js.
//
// The game files come from the server (data/index.json lists them) and are written to /data
// before main() runs; they are not part of the build. They arrive as one archive,
// data/game.tar.gz, or one by one when that fails or the browser cannot unpack gzip. Scores, key settings and saved games go
// to /save, an IndexedDB-backed directory. It is loaded before main() runs and flushed after
// every write (save_file in src/kdata.c calls Module.syncSaves).

Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  FS.mkdir('/save');
  FS.mount(IDBFS, {}, '/save');
  addRunDependency('keen-saves');
  FS.syncfs(true, function (err) {
    if (err) console.warn('Loading saved games failed:', err);
    removeRunDependency('keen-saves');
  });
});

// The files of a ustar archive (plain files only), as { name, data }.
function keenUntar(bytes) {
  var files = [], off = 0;
  while (off + 512 <= bytes.length && bytes[off] !== 0) {
    var name = '';
    for (var i = off; i < off + 100 && bytes[i]; i++) name += String.fromCharCode(bytes[i]);
    var size = parseInt(String.fromCharCode.apply(null, bytes.subarray(off + 124, off + 136)), 8) || 0;
    var type = bytes[off + 156];
    off += 512;
    if (type === 48 || type === 0) files.push({ name: name, data: bytes.slice(off, off + size) });
    off += Math.ceil(size / 512) * 512;
  }
  return files;
}

// Fetches the game files into /data. When the server has none (or a request fails) the run
// dependency stays, so the game never starts, and Module.onDataError tells the page why.
Module.preRun.push(function () {
  addRunDependency('keen-data');
  function fail(what) {
    console.warn('Loading the game files failed:', what);
    if (Module.onDataError) Module.onDataError(what);
  }
  // Relative URLs: the page may live under a path prefix.
  function oneByOne(index) {
    return Promise.all(
      index.files.map(function (f) {
        return fetch('data/' + encodeURIComponent(f.name)).then(function (r) {
          if (!r.ok) throw new Error(f.name + ': ' + r.status);
          return r.arrayBuffer().then(function (buf) {
            return { name: f.name, data: new Uint8Array(buf) };
          });
        });
      })
    );
  }
  function bundle(index) {
    if (typeof DecompressionStream === 'undefined') return Promise.reject(new Error('no gzip'));
    return fetch('data/game.tar.gz')
      .then(function (r) {
        if (!r.ok || !r.body) throw new Error('data/game.tar.gz: ' + r.status);
        return new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
      })
      .then(function (buf) {
        var files = keenUntar(new Uint8Array(buf));
        var got = {};
        files.forEach(function (f) { got[f.name] = true; });
        index.files.forEach(function (f) {
          if (!got[f.name]) throw new Error('data/game.tar.gz has no ' + f.name);
        });
        return files;
      });
  }
  fetch('data/index.json')
    .then(function (r) {
      if (!r.ok) throw new Error('data/index.json: ' + r.status);
      return r.json();
    })
    .then(function (index) {
      if (!index.complete || !index.files.length) throw new Error('missing');
      return bundle(index).catch(function (e) {
        console.warn('Loading data/game.tar.gz failed, fetching the files one by one:', e);
        return oneByOne(index);
      });
    })
    .then(function (files) {
      FS.mkdir('/data');
      files.forEach(function (f) { FS.writeFile('/data/' + f.name, f.data); });
      removeRunDependency('keen-data');
    })
    .catch(function (e) { fail(String(e && e.message || e)); });
});

(function () {
  var busy = false, again = false;
  Module.syncSaves = function () {
    if (busy) {
      again = true;
      return;
    }
    busy = true;
    FS.syncfs(false, function (err) {
      busy = false;
      if (err) console.warn('Saving to IndexedDB failed:', err);
      if (again) {
        again = false;
        Module.syncSaves();
      }
    });
  };
})();

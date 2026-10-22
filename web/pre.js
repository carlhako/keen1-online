// Browser glue for the Emscripten build, included with --pre-js.
//
// The game files come from the server (data/index.json lists them) and are written to /data
// before main() runs; they are not part of the build. Scores, key settings and saved games go
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

// Fetches the game files into /data. When the server has none (or a request fails) the run
// dependency stays, so the game never starts, and Module.onDataError tells the page why.
Module.preRun.push(function () {
  addRunDependency('keen-data');
  function fail(what) {
    console.warn('Loading the game files failed:', what);
    if (Module.onDataError) Module.onDataError(what);
  }
  // Relative URLs: the page may live under a path prefix.
  fetch('data/index.json')
    .then(function (r) {
      if (!r.ok) throw new Error('data/index.json: ' + r.status);
      return r.json();
    })
    .then(function (index) {
      if (!index.complete || !index.files.length) throw new Error('missing');
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

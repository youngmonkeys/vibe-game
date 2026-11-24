// --pre-js for the page (plan 10). Settings live under
// SDL_GetPrefPath(org, name), which SDL 3.4.18 puts at
// /libsdl/<org>/<name>/ on Emscripten. This mounts IndexedDB
// (IDBFS) on /libsdl and loads it behind a run dependency, so the settings
// file is there when SDL_AppInit reads it. src/platform/Prefs.cpp syncs back
// after every write while Module.vibePrefsPersistent is true.
//
// A private window may refuse IndexedDB. Then the mount is skipped or the
// load fails, settings last for this page only, and the game still starts.
Module.preRun = [].concat(Module.preRun || []);
Module.preRun.push(function () {
  var root = '/libsdl';
  // With ASSERTIONS, IDBFS asserts (aborts) when indexedDB is missing, so
  // check first.
  if (typeof indexedDB === 'undefined' || !indexedDB) {
    console.warn('WARN: prefs: IndexedDB is not available; settings last for this page only');
    return;
  }
  try {
    FS.mkdir(root);
    FS.mount(IDBFS, {}, root);
  } catch (error) {
    console.warn('WARN: prefs: cannot mount IndexedDB at ' + root + ' (' + error + '); settings last for this page only');
    return;
  }
  addRunDependency('vibe-idbfs');
  FS.syncfs(true, function (error) {
    if (error) {
      console.warn('WARN: prefs: loading from IndexedDB failed (' + error + '); settings last for this page only');
    } else {
      Module.vibePrefsPersistent = true;
    }
    removeRunDependency('vibe-idbfs');
  });
});

// Page script for platform/web/shell.html. It configures the Emscripten
// Module before the generated game .js loads, shows load progress and
// fatal errors in the status line, and shows the stopped message on exit.
(function () {
  'use strict';

  var canvas = document.getElementById('canvas');
  var statusLine = document.getElementById('status');
  var lastError = '';
  var stopped = false;

  function showStatus(text, isError) {
    statusLine.textContent = text;
    statusLine.hidden = !text;
    statusLine.classList.toggle('error', !!isError);
  }

  // `reason` is empty for a clean stop.
  function stop(reason) {
    stopped = true;
    var message = 'Stopped. Reload to start again.';
    showStatus(reason ? reason + '\n' + message : message, !!reason);
  }

  // Browser shortcuts stay the browser's (plan 07). SDL's key handler calls
  // preventDefault on every key it posts, which swallows F5 and Ctrl+R. This
  // capture listener on window runs before SDL's bubble listener on window;
  // for a shortcut it stops the event there, so SDL never sees it and the
  // browser acts on it. Ctrl/Cmd with an editing key (select all, copy, paste,
  // cut, undo, redo, word moves and deletes) stays with the page's text fields.
  var editingKeys = /^([acvxyz]|ArrowLeft|ArrowRight|ArrowUp|ArrowDown|Backspace|Delete|Home|End|Control|Meta|Shift|Alt)$/i;
  function isBrowserShortcut(event) {
    if (event.key === 'F5' || event.key === 'F11' || event.key === 'F12') {
      return true;
    }
    return (event.ctrlKey || event.metaKey) && !editingKeys.test(event.key);
  }
  // Paste (plan 11): SDL would call preventDefault on Ctrl/Cmd+V, and the
  // browser would then fire no `paste` event. Let the browser have the key;
  // src/platform/Paste.cpp reads the event's text for the focused field.
  function isPaste(event) {
    return (event.ctrlKey || event.metaKey) && (event.key === 'v' || event.key === 'V');
  }
  function passShortcut(event) {
    if (isBrowserShortcut(event) || isPaste(event)) {
      event.stopImmediatePropagation();
    }
  }
  window.addEventListener('keydown', passShortcut, true);
  window.addEventListener('keyup', passShortcut, true);

  canvas.addEventListener('contextmenu', function (event) {
    event.preventDefault();
  });
  canvas.addEventListener('webglcontextlost', function (event) {
    event.preventDefault();
    showStatus('The WebGL context was lost. Reload to start again.', true);
  });

  // Dev builds only; CMake writes ON or OFF here (VIBE_DEV). Plan 06:
  // `?join-lobby=<user>` runs the session check. The password is asked for
  // with a prompt, so it never sits in the URL or the browser history.
  // Plan 11: the page opens the shipping UI. On the dev page `?online`
  // still opens plan 07's ImGui lobby and `?offline` plan 05's offline arena.
  var devShell = '@VIBE_DEV_SHELL@' === 'ON';
  var programArguments = [];
  if (devShell) {
    var query = new URLSearchParams(window.location.search);
    if (query.has('online')) {
      programArguments.push('--online');
    }
    if (query.has('offline')) {
      programArguments.push('--offline');
    }
    var joinLobbyUser = query.get('join-lobby');
    if (joinLobbyUser) {
      var password = window.prompt('Password for ' + joinLobbyUser);
      if (password) {
        programArguments = ['--join-lobby', joinLobbyUser, password];
      }
      password = null;
    }
  }

  window.Module = {
    canvas: canvas,
    arguments: programArguments,
    // main() has copied the arguments; drop the page's copy of the password.
    postRun: [function () {
      programArguments.length = 0;
    }],

    // src/app/Log.cpp writes errors to stderr and everything else to stdout,
    // each line starting with its priority.
    print: function (text) {
      if (text.startsWith('WARN: ')) {
        console.warn(text);
      } else {
        console.log(text);
      }
    },
    printErr: function (text) {
      console.error(text);
      lastError = text.replace(/^(ERROR|CRITICAL): /, '');
    },

    // Emscripten reports preload progress here, and '' once main runs.
    setStatus: function (text) {
      if (!stopped) {
        showStatus(text, false);
      }
    },

    // The SDL callbacks returned success or failure and the runtime exited.
    onExit: function (code) {
      stop(code === 0 ? '' : 'Error: ' + (lastError || 'exit code ' + code));
    },
    onAbort: function (what) {
      stop('Crashed: ' + (lastError || what));
    },
  };
})();

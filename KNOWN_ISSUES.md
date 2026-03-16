# Xcaca Known Issues

## Ctrl+C shutdown hangs until a client connects

**Status:** Open
**Severity:** Minor (workaround exists)

When pressing Ctrl+C in the Xcaca terminal, the server receives the event and begins shutdown (`GiveUp` is called, `dispatchException` is set), but the process does not exit until something triggers I/O on the X11 socket (e.g., a client connecting, or `pkill -x Xcaca` from another terminal).


**Workarounds:**
- Kill from another terminal: `pkill -x Xcaca`
- Attempt to run any X client (e.g., `DISPLAY=:1 xeyes`) to unstick the shutdown

**Notes:**
- The `CACA_KEY_CTRL_C` event IS received (confirmed via log)
- `GiveUp(SIGTERM)` IS called (sets `dispatchException |= DE_TERMINATE`)
- Terminal restoration (termios, escape sequences) works correctly when the process finally exits
- The server's `atexit(caca_host_fini)` handler runs on all exit paths

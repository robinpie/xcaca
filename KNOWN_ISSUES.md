# Xcaca Known Issues

## Ctrl+C shutdown hangs until a client connects

**Status:** Open
**Severity:** Minor (workaround exists)

When pressing Ctrl+C in the Xcaca terminal, the server receives the event and begins shutdown (`GiveUp` is called, `dispatchException` is set), but the process does not exit until something triggers I/O on the X11 socket (e.g., a client connecting, or `pkill -x Xcaca` from another terminal).

**Root cause (suspected):** The X server's main loop calls `WaitForSomething()` which blocks in `ospoll_wait()`/`select()`. Although `GiveUp()` sets the `dispatchException` flag from within the BlockHandler, something in the teardown path re-enters a blocking wait before the flag is checked, or the graceful shutdown path itself blocks waiting for client activity.

**Workarounds:**
- Kill from another terminal: `pkill -x Xcaca`
- Run any X client (e.g., `DISPLAY=:1 xeyes`) to unstick the shutdown

**Notes:**
- The `CACA_KEY_CTRL_C` event IS received (confirmed via log)
- `GiveUp(SIGTERM)` IS called (sets `dispatchException |= DE_TERMINATE`)
- Terminal restoration (termios, escape sequences) works correctly when the process finally exits
- The server's `atexit(caca_host_fini)` handler runs on all exit paths

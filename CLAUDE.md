# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

**Install build dependencies (Arch/CachyOS):**
```bash
sudo pacman -S gtk4 libsoup3 json-glib meson ninja
pip install -U openai-whisper   # required at runtime on PATH
```

**Build:**
```bash
meson setup build
ninja -C build
```

**Run:**
```bash
./build/WhisperDrop
```

**Rebuild after source changes:**
```bash
ninja -C build
```

There are no automated tests.

## Architecture

The app is a GTK4 C program split across four source files in `src/`. The Python original (`whisperdrop_gui_fixed.py`) is kept for reference but is no longer the active implementation.

**Source files:**

- `src/main.c` — entry point; creates `GtkApplication` and `AppState`, connects the `activate` signal.
- `src/app.h` / `src/app.c` — `AppState` struct (owns all GTK widgets and runtime state), `Job` struct, all UI construction (`app_activate`), every button/drag-drop callback, settings persistence via `GKeyFile` (`~/.config/WhisperDrop/config.ini`), and the 500 ms status heartbeat timer.
- `src/worker.h` / `src/worker.c` — runs `whisper` in a `GThread`. Uses `g_spawn_async_with_fds` with both stdout and stderr directed to the same pipe (merged), then reads line-by-line with `fgets` (blocking). All UI updates flow back to the main thread via `g_idle_add`. Cancellation sends `SIGTERM` to the child PID (stored under `worker_mutex`) and is detected when `fgets` returns EOF after the process exits.
- `src/updater.h` / `src/updater.c` — checks `api.github.com/repos/mgalde/WhisperDrop/releases/latest` with `libsoup3` async (`soup_session_send_and_read_async`), parses the response with `json-glib`, and on Linux performs a self-update via `rename(2)` (atomic replace of the running binary). Skips `.exe`/`.dmg`/`.zip` assets when picking the download URL.

**Threading model:** only the main GLib/GTK main loop touches GTK widgets. The worker thread posts `LogMsg`, `JobUpdateMsg`, `ProgressMsg`, and `DoneMsg` structs to the main thread using `g_idle_add`. `GMutex` protects `cancel_requested` and `current_pid`.

**Build system:** `meson.build` at the project root. `data/whisperdrop.gresource.xml` bundles `WhisperDrop.png` as a GResource (prefix `/com/saguarosec/whisperdrop`), accessed in code via `gdk_texture_new_from_resource(ICON_RESOURCE)`.

**Settings** persist to `~/.config/WhisperDrop/config.ini` via `GKeyFile`; loaded in `app_load_settings`, saved on window close and in `app_state_free`.

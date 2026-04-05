#pragma once
#include "app.h"

/* Start the whisper worker thread.
   Reads the current model/format/folder settings from app's UI widgets.
   Must be called from the main thread. */
void worker_start (AppState *app);

/* Request cancellation of the current run (thread-safe).
   Sends SIGTERM to the running whisper process if one is active. */
void worker_cancel (AppState *app);

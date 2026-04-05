#pragma once
#include "app.h"

/* Check GitHub Releases for a newer version of WhisperDrop.
   Runs asynchronously via libsoup3.
   If show_if_current is FALSE the check is silent when already up to date
   (used for the automatic startup check). */
void updater_check (AppState *app, gboolean show_if_current);

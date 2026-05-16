#include <gtk/gtk.h>
#include "app.h"

int main(int argc, char *argv[]) {
    GtkApplication *gapp = gtk_application_new(
        APP_ID,
        G_APPLICATION_DEFAULT_FLAGS);

    AppState *app = app_state_new();
    g_signal_connect(gapp, "activate", G_CALLBACK(app_activate), app);

    int status = g_application_run(G_APPLICATION(gapp), argc, argv);

    app_state_free(app);
    g_object_unref(gapp);
    return status;
}

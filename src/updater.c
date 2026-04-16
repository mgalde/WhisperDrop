#include "updater.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================
   Version comparison
   ========================================================== */

static void parse_version(const gchar *v, int *maj, int *min, int *pat) {
    *maj = *min = *pat = 0;
    if (v) sscanf(v, "%d.%d.%d", maj, min, pat);
}

static gboolean version_newer(const gchar *latest, const gchar *current) {
    int lmaj, lmin, lpat, cmaj, cmin, cpat;
    parse_version(latest,  &lmaj, &lmin, &lpat);
    parse_version(current, &cmaj, &cmin, &cpat);
    if (lmaj != cmaj) return lmaj > cmaj;
    if (lmin != cmin) return lmin > cmin;
    return lpat > cpat;
}

/* ==========================================================
   Self-update  (Linux: atomic rename)
   ========================================================== */

static void apply_update(AppState *app, const gchar *tmp_path, const gchar *version) {
    char exe_buf[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (len <= 0) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Could not determine the running executable path.\n\n"
            "The update was saved to:\n%s\n\n"
            "Please replace the binary manually.", tmp_path);
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        return;
    }
    exe_buf[len] = '\0';

    chmod(tmp_path, 0755);

    gboolean replaced  = (rename(tmp_path, exe_buf) == 0);
    int      ren_errno = errno;

    if (!replaced && ren_errno == EXDEV) {
        /* tmp and the binary are on different filesystems (e.g. /tmp is tmpfs).
           Stage a copy inside the binary's own directory, then rename atomically. */
        gchar *exe_dir = g_path_get_dirname(exe_buf);
        gchar *stage   = g_strdup_printf("%s/.whisperdrop-update-XXXXXX", exe_dir);
        int    sfd     = mkstemp(stage);
        g_free(exe_dir);
        if (sfd >= 0) {
            close(sfd);
            GFile  *src = g_file_new_for_path(tmp_path);
            GFile  *dst = g_file_new_for_path(stage);
            GError *ce  = NULL;
            if (g_file_copy(src, dst,
                    G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS,
                    NULL, NULL, NULL, &ce)) {
                chmod(stage, 0755);
                replaced = (rename(stage, exe_buf) == 0);
            }
            if (ce) g_error_free(ce);
            g_object_unref(src);
            g_object_unref(dst);
            if (!replaced) unlink(stage);
        }
        g_free(stage);
        unlink(tmp_path);   /* original temp cleaned up either way */
    }

    if (!replaced) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Could not replace the executable.\n\n"
            "Make sure you have write permission to:\n%s\n\n"
            "Or download the update manually.", exe_buf);
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        if (ren_errno != EXDEV) unlink(tmp_path);   /* EXDEV path already cleaned up */
        return;
    }

    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "WhisperDrop %s has been installed.\n\n"
        "Please restart the app to use the new version.", version);
    gtk_window_present(GTK_WINDOW(dlg));
    g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
}

/* ==========================================================
   Download
   ========================================================== */

typedef struct {
    AppState *app;
    gchar    *version;
    gchar    *url;
} DownloadData;

static void on_download_response(GObject *source, GAsyncResult *result, gpointer data) {
    DownloadData *dd      = data;
    SoupSession  *session = SOUP_SESSION(source);

    GError *err  = NULL;
    GBytes *body = soup_session_send_and_read_finish(session, result, &err);

    /* Silently discard if the window was closed while downloading */
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error(&err);
        if (body) g_bytes_unref(body);
        goto cleanup;
    }

    if (err || !body) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(dd->app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Download failed.\n\n%s", err ? err->message : "Unknown error");
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        g_clear_error(&err);
        if (body) g_bytes_unref(body);
        goto cleanup;
    }

    /* Write binary to a temp file */
    gchar *tmp_path = NULL;
    GError *tmp_err = NULL;
    gint    tmp_fd  = g_file_open_tmp("whisperdrop-update-XXXXXX", &tmp_path, &tmp_err);
    if (tmp_fd < 0) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(dd->app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Could not create temporary file.\n\n%s",
            tmp_err ? tmp_err->message : "Unknown error");
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        g_clear_error(&tmp_err);
        g_bytes_unref(body);
        goto cleanup;
    }

    gsize         data_len = 0;
    const guint8 *data_ptr = g_bytes_get_data(body, &data_len);

    /* Write in a loop — write(2) can return short on signals or full buffers.
       A partial write would corrupt the binary being replaced. */
    gsize    written  = 0;
    gboolean write_ok = TRUE;
    while (written < data_len) {
        ssize_t n = write(tmp_fd, data_ptr + written, data_len - written);
        if (n <= 0) { write_ok = FALSE; break; }
        written += (gsize)n;
    }
    close(tmp_fd);
    g_bytes_unref(body);

    if (!write_ok) {
        unlink(tmp_path);
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(dd->app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Could not write the downloaded update to disk.\n\n"
            "Check available disk space and try again.");
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        g_free(tmp_path);
        goto cleanup;
    }

    apply_update(dd->app, tmp_path, dd->version);
    g_free(tmp_path);

cleanup:
    g_object_unref(session);
    g_free(dd->version);
    g_free(dd->url);
    g_free(dd);
}

static void start_download(AppState *app, const gchar *version, const gchar *url) {
    DownloadData *dd = g_new0(DownloadData, 1);
    dd->app     = app;
    dd->version = g_strdup(version);
    dd->url     = g_strdup(url);

    SoupSession *session = soup_session_new();
    SoupMessage *msg     = soup_message_new("GET", url);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                                "User-Agent", APP_NAME "/" APP_VERSION);
    soup_session_send_and_read_async(session, msg, G_PRIORITY_DEFAULT,
                                     dd->app->update_cancel, on_download_response, dd);
    g_object_unref(msg);
}

/* ==========================================================
   Update dialog response
   ========================================================== */

typedef struct {
    AppState *app;
    gchar    *version;
    gchar    *url;
    gboolean  direct;  /* TRUE = direct binary download, FALSE = open release page */
} UpdateDialogData;

static void on_update_dialog_response(GtkDialog *dlg, int response, gpointer data) {
    UpdateDialogData *ud = data;
    if (response == GTK_RESPONSE_YES) {
        if (ud->direct)
            start_download(ud->app, ud->version, ud->url);
        else
            g_app_info_launch_default_for_uri(ud->url, NULL, NULL);
    }
    g_free(ud->version);
    g_free(ud->url);
    g_free(ud);
    gtk_window_destroy(GTK_WINDOW(dlg));
}

/* ==========================================================
   GitHub Releases check
   ========================================================== */

/* Pick the main WhisperDrop binary URL from the release assets.
 * Rules (in priority order):
 *   1. Must not be the installer or uninstaller binary.
 *   2. Must not be a Windows (.exe), macOS (.dmg), or archive (.zip/.tar*) file.
 *   3. Take the first asset whose name starts with "WhisperDrop-" and passes. */
static gchar *pick_asset_url(JsonArray *assets) {
    guint n = json_array_get_length(assets);
    for (guint i = 0; i < n; i++) {
        JsonObject  *asset = json_array_get_object_element(assets, i);
        const gchar *name  = json_object_get_string_member(asset, "name");
        const gchar *url   = json_object_get_string_member(asset, "browser_download_url");
        if (!name || !url || url[0] == '\0') continue;
        /* Skip installer and uninstaller assets */
        if (strstr(name, "Installer")   != NULL) continue;
        if (strstr(name, "Uninstaller") != NULL) continue;
        /* Skip non-Linux and archive formats */
        if (g_str_has_suffix(name, ".exe"))  continue;
        if (g_str_has_suffix(name, ".dmg"))  continue;
        if (g_str_has_suffix(name, ".zip"))  continue;
        if (g_str_has_suffix(name, ".tar"))  continue;
        if (strstr(name, ".tar.") != NULL)   continue;
        /* Must be the main binary: name starts with "WhisperDrop-" */
        if (!g_str_has_prefix(name, "WhisperDrop-")) continue;
        return g_strdup(url);
    }
    return NULL;
}

typedef struct {
    AppState *app;
    gboolean  show_if_current;
} CheckData;

static void on_check_response(GObject *source, GAsyncResult *result, gpointer data) {
    CheckData   *cd      = data;
    SoupSession *session = SOUP_SESSION(source);

    GError *err  = NULL;
    GBytes *body = soup_session_send_and_read_finish(session, result, &err);

    /* Silently discard if the window was closed while the request was in flight */
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error(&err);
        if (body) g_bytes_unref(body);
        goto cleanup;
    }

    if (err || !body) {
        if (cd->show_if_current) {
            GtkWidget *dlg = gtk_message_dialog_new(
                GTK_WINDOW(cd->app->window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Could not check for updates.\n\n%s",
                err ? err->message : "No response");
            gtk_window_present(GTK_WINDOW(dlg));
            g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        }
        g_clear_error(&err);
        if (body) g_bytes_unref(body);
        goto cleanup;
    }

    /* Parse JSON */
    gsize         json_len = 0;
    const gchar  *json_str = g_bytes_get_data(body, &json_len);
    JsonParser   *parser   = json_parser_new();
    GError       *perr     = NULL;
    json_parser_load_from_data(parser, json_str, (gssize)json_len, &perr);

    if (perr) {
        if (cd->show_if_current) {
            GtkWidget *dlg = gtk_message_dialog_new(
                GTK_WINDOW(cd->app->window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Update check failed: could not parse server response.\n\n%s",
                perr->message);
            gtk_window_present(GTK_WINDOW(dlg));
            g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        }
        g_error_free(perr);
        g_object_unref(parser);
        g_bytes_unref(body);
        goto cleanup;
    }

    JsonObject  *root = json_node_get_object(json_parser_get_root(parser));
    const gchar *tag  = json_object_get_string_member(root, "tag_name");
    if (!tag) tag = "";

    /* Strip leading 'v' */
    while (*tag == 'v') tag++;

    if (tag[0] == '\0') {
        if (cd->show_if_current) {
            GtkWidget *dlg = gtk_message_dialog_new(
                GTK_WINDOW(cd->app->window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Update check failed: no version tag in response.");
            gtk_window_present(GTK_WINDOW(dlg));
            g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        }
        g_object_unref(parser);
        g_bytes_unref(body);
        goto cleanup;
    }

    if (version_newer(tag, APP_VERSION)) {
        /* Find the best download URL */
        gchar *download_url = NULL;
        if (json_object_has_member(root, "assets")) {
            JsonArray *assets = json_object_get_array_member(root, "assets");
            download_url = pick_asset_url(assets);
        }
        if (!download_url) {
            const gchar *html = json_object_get_string_member(root, "html_url");
            download_url = html && html[0]
                ? g_strdup(html)
                : g_strdup_printf("https://github.com/%s/releases", GITHUB_REPO);
        }

        /* GitHub release download URLs contain /releases/download/; page
         * URLs contain /releases/tag/ or /releases/ alone. */
        gboolean is_direct = strstr(download_url, "/releases/download/") != NULL;

        UpdateDialogData *ud = g_new0(UpdateDialogData, 1);
        ud->app     = cd->app;
        ud->version = g_strdup(tag);
        ud->url     = download_url;   /* ownership transferred */
        ud->direct  = is_direct;

        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(cd->app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "WhisperDrop %s is available \u2014 you have %s.\n\n%s",
            tag, APP_VERSION,
            is_direct ? "Download and install now?"
                      : "Open the download page?");
        gtk_window_set_title(GTK_WINDOW(dlg), "Update Available");
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(on_update_dialog_response), ud);

    } else if (cd->show_if_current) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(cd->app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "You\u2019re on the latest version (%s).", APP_VERSION);
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
    }

    g_object_unref(parser);
    g_bytes_unref(body);

cleanup:
    g_object_unref(session);
    g_free(cd);
}

/* ==========================================================
   Public API
   ========================================================== */

void updater_check(AppState *app, gboolean show_if_current) {
    CheckData *cd      = g_new0(CheckData, 1);
    cd->app            = app;
    cd->show_if_current = show_if_current;

    gchar *api_url = g_strdup_printf(
        "https://api.github.com/repos/%s/releases/latest", GITHUB_REPO);

    SoupSession *session = soup_session_new();
    SoupMessage *msg     = soup_message_new("GET", api_url);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                                "User-Agent", APP_NAME "/" APP_VERSION);
    soup_session_send_and_read_async(session, msg, G_PRIORITY_DEFAULT,
                                     cd->app->update_cancel, on_check_response, cd);
    g_object_unref(msg);
    g_free(api_url);
}

#include "app.h"
#include "worker.h"
#include "updater.h"
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <pango/pango.h>
#include <stdarg.h>
#include <string.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#endif

typedef struct {
    const gchar *id;
    const gchar *label;
    const gchar *css_class;
} ThemeOption;

static const ThemeOption THEME_OPTIONS[] = {
    { "system",   "System",   NULL },
    { "graphite", "Graphite", "theme-graphite" },
    { "sand",     "Sand",     "theme-sand" },
    { "forest",   "Forest",   "theme-forest" },
};

static const guint THEME_COUNT = G_N_ELEMENTS(THEME_OPTIONS);

static guint theme_index_from_id(const gchar *id) {
    for (guint i = 0; i < THEME_COUNT; i++) {
        if (g_strcmp0(THEME_OPTIONS[i].id, id) == 0)
            return i;
    }
    return 0;
}

static const ThemeOption *theme_option_from_index(guint index) {
    return &THEME_OPTIONS[index < THEME_COUNT ? index : 0];
}

static void app_apply_theme(AppState *app, const gchar *theme_id) {
    const ThemeOption *theme = theme_option_from_index(theme_index_from_id(theme_id));
    GtkSettings *settings = gtk_settings_get_default();

    if (app->window) {
        for (guint i = 1; i < THEME_COUNT; i++)
            gtk_widget_remove_css_class(GTK_WIDGET(app->window), THEME_OPTIONS[i].css_class);
    }

    if (settings) {
        if (!app->theme_pref_initialized) {
            g_object_get(settings, "gtk-application-prefer-dark-theme",
                         &app->system_prefer_dark, NULL);
            app->theme_pref_initialized = TRUE;
        }
        gboolean prefer_dark =
            g_strcmp0(theme->id, "system") == 0
                ? app->system_prefer_dark
                : g_strcmp0(theme->id, "graphite") == 0 ||
                  g_strcmp0(theme->id, "forest") == 0;
        g_object_set(settings, "gtk-application-prefer-dark-theme", prefer_dark, NULL);
    }

    if (app->window && theme->css_class)
        gtk_widget_add_css_class(GTK_WIDGET(app->window), theme->css_class);
}

/* ==========================================================
   Job helpers
   ========================================================== */

Job *job_new(const gchar *path) {
    Job *j   = g_new0(Job, 1);
    j->path  = g_strdup(path);
    j->status = JOB_QUEUED;
    return j;
}

void job_free(Job *job) {
    if (!job) return;
    g_free(job->path);
    g_free(job->out_dir);
    g_free(job->error_msg);
    /* row and label are GTK widgets owned by the list box — not freed here */
    g_free(job);
}

const gchar *job_status_str(JobStatus s) {
    switch (s) {
        case JOB_QUEUED:    return "Queued";
        case JOB_RUNNING:   return "Running";
        case JOB_DONE:      return "Done";
        case JOB_ERROR:     return "Error";
        case JOB_CANCELLED: return "Cancelled";
        default:            return "?";
    }
}

gchar *job_label_text(const Job *job) {
    gchar *base = g_path_get_basename(job->path);
    gchar *text = g_strdup_printf("[%s]  %s  —  %s",
                                  job_status_str(job->status), base, job->path);
    g_free(base);
    return text;
}

/* ==========================================================
   AppState lifecycle
   ========================================================== */

static gchar *make_settings_path(void) {
    gchar *dir  = g_build_filename(g_get_user_config_dir(), "WhisperDrop", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *path = g_build_filename(dir, "config.ini", NULL);
    g_free(dir);
    return path;
}

AppState *app_state_new(void) {
    AppState *app    = g_new0(AppState, 1);
    app->jobs        = g_ptr_array_new_with_free_func((GDestroyNotify)job_free);
    g_mutex_init(&app->worker_mutex);
    app->keyfile        = g_key_file_new();
    app->settings_path  = make_settings_path();
    app->update_cancel  = g_cancellable_new();
    app->theme_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(app->theme_provider,
                                        "/com/saguarosec/whisperdrop/data/whisperdrop.css");
    return app;
}

void app_state_free(AppState *app) {
    if (!app) return;
    g_ptr_array_free(app->jobs, TRUE);
    g_mutex_clear(&app->worker_mutex);
    g_key_file_free(app->keyfile);
    g_free(app->settings_path);
    g_object_unref(app->update_cancel);
    g_object_unref(app->theme_provider);
    g_free(app);
}

/* ==========================================================
   Settings
   ========================================================== */

void app_load_settings(AppState *app) {
    g_key_file_load_from_file(app->keyfile, app->settings_path, G_KEY_FILE_NONE, NULL);

    /* model */
    gchar *model = g_key_file_get_string(app->keyfile, "Settings", "model", NULL);
    if (!model || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->model_combo), model))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->model_combo), "turbo");
    g_free(model);

    /* format */
    gchar *fmt = g_key_file_get_string(app->keyfile, "Settings", "format", NULL);
    if (!fmt || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->format_combo), fmt))
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->format_combo), 1); /* default: txt */
    g_free(fmt);

    /* same_folder */
    GError   *err       = NULL;
    gboolean  same      = g_key_file_get_boolean(app->keyfile, "Settings", "same_folder", &err);
    if (err) { same = TRUE; g_error_free(err); }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->same_folder_check), same);

    /* out_folder */
    gchar *outf = g_key_file_get_string(app->keyfile, "Settings", "out_folder", NULL);
    if (outf) { gtk_editable_set_text(GTK_EDITABLE(app->out_folder_entry), outf); g_free(outf); }

    /* extra_args */
    gchar *extra = g_key_file_get_string(app->keyfile, "Settings", "extra_args", NULL);
    if (extra) { gtk_editable_set_text(GTK_EDITABLE(app->extra_args_entry), extra); g_free(extra); }

    /* theme */
    gchar *theme = g_key_file_get_string(app->keyfile, "Settings", "theme", NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->theme_combo), theme_index_from_id(theme));
    app_apply_theme(app, theme);
    g_free(theme);
}

void app_save_settings(AppState *app) {
    if (!app->model_combo) return;  /* called before UI exists */

    gchar       *model = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->model_combo));
    gchar       *fmt   = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->format_combo));
    gboolean     same  = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->same_folder_check));
    const gchar *outf  = gtk_editable_get_text(GTK_EDITABLE(app->out_folder_entry));
    const gchar *extra = gtk_editable_get_text(GTK_EDITABLE(app->extra_args_entry));
    guint        theme_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->theme_combo));
    const ThemeOption *theme = theme_option_from_index(theme_idx);

    g_key_file_set_string (app->keyfile, "Settings", "model",       model ? model : "turbo");
    g_key_file_set_string (app->keyfile, "Settings", "format",      fmt   ? fmt   : "txt");
    g_key_file_set_boolean(app->keyfile, "Settings", "same_folder", same);
    g_key_file_set_string (app->keyfile, "Settings", "out_folder",  outf  ? outf  : "");
    g_key_file_set_string (app->keyfile, "Settings", "extra_args",  extra ? extra : "");
    g_key_file_set_string (app->keyfile, "Settings", "theme",       theme->id);

    g_key_file_save_to_file(app->keyfile, app->settings_path, NULL);
    g_free(model);
    g_free(fmt);
}

/* ==========================================================
   UI helpers
   ========================================================== */

void app_add_path(AppState *app, const gchar *path) {
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) return;

    /* deduplicate */
    for (guint i = 0; i < app->jobs->len; i++) {
        if (g_str_equal(((Job *)app->jobs->pdata[i])->path, path)) return;
    }

    Job *job = job_new(path);
    g_ptr_array_add(app->jobs, job);

    /* Build list row */
    GtkWidget *row   = gtk_list_box_row_new();
    GtkWidget *label = gtk_label_new(NULL);
    gchar     *text  = job_label_text(job);
    gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_widget_set_margin_top(label, 3);
    gtk_widget_set_margin_bottom(label, 3);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(app->file_list, row);

    job->row   = row;
    job->label = label;
}

void app_refresh_row(AppState *app, guint idx) {
    if (idx >= app->jobs->len) return;
    Job *job = app->jobs->pdata[idx];
    if (!job->label) return;
    gchar *text = job_label_text(job);
    gtk_label_set_text(GTK_LABEL(job->label), text);
    g_free(text);
}

void app_append_log(AppState *app, const gchar *text) {
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->log_buf, &iter);
    gtk_text_buffer_insert(app->log_buf, &iter, text, -1);

    /* auto-scroll to bottom */
    gtk_text_buffer_get_end_iter(app->log_buf, &iter);
    GtkTextMark *mark = gtk_text_buffer_get_mark(app->log_buf, "_wd_end");
    if (!mark)
        mark = gtk_text_buffer_create_mark(app->log_buf, "_wd_end", &iter, FALSE);
    else
        gtk_text_buffer_move_mark(app->log_buf, mark, &iter);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app->log_view), mark, 0.0, TRUE, 0.0, 1.0);
}

void app_set_status(AppState *app, const gchar *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gchar *msg = g_strdup_vprintf(fmt, args);
    va_end(args);
    gtk_label_set_text(GTK_LABEL(app->status_label), msg);
    g_free(msg);
}

void app_set_running(AppState *app, gboolean running) {
    gtk_widget_set_sensitive(app->start_btn,         !running);
    gtk_widget_set_sensitive(app->stop_btn,           running);
    gtk_widget_set_sensitive(app->add_btn,           !running);
    gtk_widget_set_sensitive(app->remove_btn,        !running);
    gtk_widget_set_sensitive(app->clear_btn,         !running);
    gtk_widget_set_sensitive(app->model_combo,       !running);
    gtk_widget_set_sensitive(app->format_combo,      !running);
    gtk_widget_set_sensitive(app->same_folder_check, !running);
    gtk_widget_set_sensitive(app->extra_args_entry,  !running);

    if (!running) {
        gboolean same = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->same_folder_check));
        gtk_widget_set_sensitive(app->out_folder_entry, !same);
        gtk_widget_set_sensitive(app->out_folder_btn,   !same);
    } else {
        gtk_widget_set_sensitive(app->out_folder_entry, FALSE);
        gtk_widget_set_sensitive(app->out_folder_btn,   FALSE);
    }
}

/* ==========================================================
   Status heartbeat timer
   ========================================================== */

static gchar *format_elapsed(gint64 usec) {
    gint total_s = (gint)(usec / G_USEC_PER_SEC);
    gint h = total_s / 3600;
    gint m = (total_s % 3600) / 60;
    gint s = total_s % 60;
    return h > 0 ? g_strdup_printf("%d:%02d:%02d", h, m, s)
                 : g_strdup_printf("%02d:%02d", m, s);
}

static gboolean on_status_tick(gpointer data) {
    AppState *app = data;
    if (!app->run_started_at) return G_SOURCE_REMOVE;

    gchar *elapsed = format_elapsed(g_get_monotonic_time() - app->run_started_at);
    guint  total   = app->jobs->len;
    guint  done    = 0;
    gint   running_idx = -1;

    for (guint i = 0; i < total; i++) {
        Job *j = app->jobs->pdata[i];
        if (j->status == JOB_DONE || j->status == JOB_ERROR || j->status == JOB_CANCELLED)
            done++;
        if (j->status == JOB_RUNNING)
            running_idx = (gint)i;
    }

    if (app->stop_requested) {
        app_set_status(app, "Stopping… (%u/%u)  •  Elapsed %s", done, total, elapsed);
    } else if (running_idx >= 0) {
        gchar *base = g_path_get_basename(((Job *)app->jobs->pdata[running_idx])->path);
        app_set_status(app, "Running: %s  (%u/%u)  •  Elapsed %s", base, done, total, elapsed);
        g_free(base);
    } else {
        app_set_status(app, "Running… (%u/%u)  •  Elapsed %s", done, total, elapsed);
    }

    g_free(elapsed);
    return G_SOURCE_CONTINUE;
}

/* ==========================================================
   Drag-and-drop
   ========================================================== */

static gboolean on_drop(GtkDropTarget *target, const GValue *value,
                         double x, double y, gpointer data) {
    (void)target; (void)x; (void)y;
    AppState *app = data;
    if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) return FALSE;

    GSList *files = g_value_get_boxed(value);
    guint   added = 0;
    for (GSList *l = files; l; l = l->next) {
        gchar *path = g_file_get_path(G_FILE(l->data));
        if (path) {
            guint before = app->jobs->len;
            app_add_path(app, path);
            if (app->jobs->len > before) added++;
            g_free(path);
        }
    }
    if (added) app_set_status(app, "Added %u file(s).", added);
    return TRUE;
}

/* ==========================================================
   File chooser dialog
   ========================================================== */

static void on_add_files_response(GtkNativeDialog *dialog, int response, gpointer data) {
    AppState *app = data;
    if (response == GTK_RESPONSE_ACCEPT) {
        GListModel *files = gtk_file_chooser_get_files(GTK_FILE_CHOOSER(dialog));
        guint n     = g_list_model_get_n_items(files);
        guint added = 0;
        for (guint i = 0; i < n; i++) {
            GFile *f    = G_FILE(g_list_model_get_item(files, i));
            gchar *path = g_file_get_path(f);
            if (path) {
                guint before = app->jobs->len;
                app_add_path(app, path);
                if (app->jobs->len > before) added++;
                g_free(path);
            }
            g_object_unref(f);
        }
        g_object_unref(files);
        app_set_status(app, added ? "Added %u file(s)." : "No new files added.", added);
    }
    g_object_unref(dialog);
}

static void on_add_files_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AppState *app = data;

    GtkFileChooserNative *dlg = gtk_file_chooser_native_new(
        "Choose media files", GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "Add", "Cancel");
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio/Video");
    const gchar *pats[] = {
        "*.mp3","*.wav","*.m4a","*.flac","*.aac","*.ogg","*.opus",
        "*.wma","*.mp4","*.mkv","*.mov","*.webm","*.m4v", NULL
    };
    for (int i = 0; pats[i]; i++) gtk_file_filter_add_pattern(filter, pats[i]);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), all);

    g_signal_connect(dlg, "response", G_CALLBACK(on_add_files_response), app);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dlg));
}

/* ==========================================================
   Remove / clear
   ========================================================== */

static void on_remove_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AppState *app    = data;
    GList    *sel    = gtk_list_box_get_selected_rows(app->file_list);
    guint     removed = 0;

    for (GList *l = sel; l; l = l->next) {
        GtkWidget *row = GTK_WIDGET(l->data);
        for (guint i = 0; i < app->jobs->len; i++) {
            Job *job = app->jobs->pdata[i];
            if (job->row == row) {
                gtk_list_box_remove(app->file_list, row);
                job->row   = NULL;
                job->label = NULL;
                g_ptr_array_remove_index(app->jobs, i);
                removed++;
                break;
            }
        }
    }
    g_list_free(sel);
    if (removed) app_set_status(app, "Removed %u file(s).", removed);
}

static void on_clear_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AppState *app = data;

    if (app->worker_thread) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Stop the current run before clearing.");
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        return;
    }

    for (guint i = 0; i < app->jobs->len; i++) {
        Job *job = app->jobs->pdata[i];
        if (job->row) gtk_list_box_remove(app->file_list, job->row);
        job->row = job->label = NULL;
    }
    g_ptr_array_set_size(app->jobs, 0);
    gtk_progress_bar_set_fraction(app->progress, 0.0);
    gtk_text_buffer_set_text(app->log_buf, "", 0);
    app_set_status(app, "Cleared.");
}

/* ==========================================================
   Output folder chooser
   ========================================================== */

static void on_out_folder_response(GtkNativeDialog *dialog, int response, gpointer data) {
    AppState *app = data;
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (f) {
            gchar *path = g_file_get_path(f);
            if (path) gtk_editable_set_text(GTK_EDITABLE(app->out_folder_entry), path);
            g_free(path);
            g_object_unref(f);
        }
    }
    g_object_unref(dialog);
}

static void on_out_folder_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AppState *app = data;
    GtkFileChooserNative *dlg = gtk_file_chooser_native_new(
        "Choose output folder", GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Choose", "Cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_out_folder_response), app);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dlg));
}

static void on_same_folder_toggled(GtkCheckButton *btn, gpointer data) {
    AppState *app    = data;
    gboolean  active = gtk_check_button_get_active(btn);
    gtk_widget_set_sensitive(app->out_folder_entry, !active);
    gtk_widget_set_sensitive(app->out_folder_btn,   !active);
}

/* ==========================================================
   Open output folder
   ========================================================== */

static void on_open_out_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AppState *app = data;
    if (app->jobs->len == 0) return;

    const gchar *outf = gtk_editable_get_text(GTK_EDITABLE(app->out_folder_entry));
    gboolean     same = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->same_folder_check));

    gchar *folder = NULL;
    if (!same && outf && outf[0]) {
        folder = g_strdup(outf);
    } else {
        /* First completed job's output dir, else first job's directory */
        for (guint i = 0; i < app->jobs->len; i++) {
            Job *j = app->jobs->pdata[i];
            if (j->status == JOB_DONE && j->out_dir) {
                folder = g_strdup(j->out_dir);
                break;
            }
        }
        if (!folder)
            folder = g_path_get_dirname(((Job *)app->jobs->pdata[0])->path);
    }

    GFile *gf  = g_file_new_for_path(folder);
    gchar *uri = g_file_get_uri(gf);
    g_app_info_launch_default_for_uri(uri, NULL, NULL);
    g_free(uri);
    g_object_unref(gf);
    g_free(folder);
}

/* ==========================================================
   Start / Stop
   ========================================================== */

static void show_info(AppState *app, const gchar *msg) {
    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
    gtk_window_present(GTK_WINDOW(dlg));
    g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
}

static void on_start_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AppState *app = data;
    if (app->worker_thread) return;

    if (app->jobs->len == 0) {
        show_info(app, "Add at least one file to transcribe.");
        return;
    }

    gchar *model = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->model_combo));
    if (!model || model[0] == '\0') {
        show_info(app, "Choose a Whisper model (e.g., turbo).");
        g_free(model);
        return;
    }
    g_free(model);

    gboolean     same = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->same_folder_check));
    const gchar *outf = gtk_editable_get_text(GTK_EDITABLE(app->out_folder_entry));
    if (!same && (!outf || outf[0] == '\0')) {
        show_info(app, "Choose an output folder, or enable \u201cSave next to input file\u201d.");
        return;
    }

    /* Reset all job statuses */
    for (guint i = 0; i < app->jobs->len; i++) {
        Job *j = app->jobs->pdata[i];
        j->status = JOB_QUEUED;
        g_free(j->error_msg); j->error_msg = NULL;
        app_refresh_row(app, i);
    }

    app_set_running(app, TRUE);
    gtk_progress_bar_set_fraction(app->progress, 0.0);
    app_set_status(app, "Running\u2026");
    app_append_log(app, "\n\u23F3 Starting transcription\u2026 "
                        "(Whisper may be quiet while loading the model.)\n");

    app->run_started_at = g_get_monotonic_time();
    app->stop_requested = FALSE;
    app->status_timer_id = g_timeout_add(500, on_status_tick, app);

    worker_start(app);
}

static void on_stop_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AppState *app = data;
    app->stop_requested = TRUE;
    worker_cancel(app);
    app_set_status(app, "Stopping\u2026");
}

/* ==========================================================
   About dialog
   ========================================================== */

static void on_about_activated(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    AppState *app = data;

    GtkWidget *dlg = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), APP_NAME);
    gtk_about_dialog_set_version    (GTK_ABOUT_DIALOG(dlg), APP_VERSION);
    gtk_about_dialog_set_comments   (GTK_ABOUT_DIALOG(dlg),
        "A drag-and-drop GUI wrapper for the whisper CLI (openai/whisper).\n"
        "Runs transcription entirely locally \u2014 nothing is sent to the cloud.");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dlg), GTK_LICENSE_MIT_X11);

    /* Website button on the main page */
    gtk_about_dialog_set_website      (GTK_ABOUT_DIALOG(dlg), APP_WEBSITE);
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dlg), "saguarosec.com");

    /* Credits tab — author */
    const gchar *authors[] = { APP_AUTHOR " <" APP_EMAIL ">", NULL };
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dlg), authors);

    /* Credits tab — GitHub */
    const gchar *github[] = { APP_GITHUB, NULL };
    gtk_about_dialog_add_credit_section(GTK_ABOUT_DIALOG(dlg), "GitHub", github);

    /* Credits tab — coding assistant */
    const gchar *assistants[] = {
        "Claude Code by Anthropic",
        "https://claude.ai/code",
        NULL
    };
    gtk_about_dialog_add_credit_section(GTK_ABOUT_DIALOG(dlg),
                                        "Coding Assistant", assistants);

    /* Credits tab — open source libraries (versions from compile-time macros) */
    gchar *lib_gtk   = g_strdup_printf(
        "GTK %d.%d.%d \u2014 GUI Toolkit",
        GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION);
    gchar *lib_glib  = g_strdup_printf(
        "GLib %d.%d.%d \u2014 Core Utility Library",
        GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
    gchar *lib_pango = g_strdup_printf(
        "Pango %d.%d.%d \u2014 Text Layout & Rendering",
        PANGO_VERSION_MAJOR, PANGO_VERSION_MINOR, PANGO_VERSION_MICRO);
    gchar *lib_soup  = g_strdup_printf(
        "libsoup %d.%d.%d \u2014 HTTP & Networking",
        SOUP_MAJOR_VERSION, SOUP_MINOR_VERSION, SOUP_MICRO_VERSION);
    gchar *lib_json  = g_strdup_printf(
        "json-glib %d.%d.%d \u2014 JSON Parsing",
        JSON_MAJOR_VERSION, JSON_MINOR_VERSION, JSON_MICRO_VERSION);

    const gchar *libraries[] = {
        /* Runtime — no compile-time version available */
        "openai-whisper \u2014 Transcription Engine (runtime)",
        "https://github.com/openai/whisper",
        /* Compile-time linked libraries */
        lib_gtk,
        "https://gtk.org",
        lib_glib,
        "https://docs.gtk.org/glib/",
        lib_pango,
        "https://pango.gnome.org",
        lib_soup,
        "https://gitlab.gnome.org/GNOME/libsoup",
        lib_json,
        "https://gitlab.gnome.org/GNOME/json-glib",
        /* Runtime — no compile-time version available */
        "FFmpeg \u2014 Audio/Video Decoding (runtime)",
        "https://ffmpeg.org",
        NULL
    };
    gtk_about_dialog_add_credit_section(GTK_ABOUT_DIALOG(dlg),
                                        "Open Source Libraries", libraries);
    g_free(lib_gtk);
    g_free(lib_glib);
    g_free(lib_pango);
    g_free(lib_soup);
    g_free(lib_json);

    GdkTexture *tex = gdk_texture_new_from_resource(ICON_RESOURCE);
    if (tex) {
        gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dlg), GDK_PAINTABLE(tex));
        g_object_unref(tex);
    }

    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(app->window));
    gtk_window_present(GTK_WINDOW(dlg));
    g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
}

static void on_check_updates_activated(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    updater_check((AppState *)data, TRUE);
}

/* ==========================================================
   Window close
   ========================================================== */

static gboolean on_close_request(GtkWindow *win, gpointer data) {
    (void)win;
    AppState *app = data;

    if (app->status_timer_id) {
        g_source_remove(app->status_timer_id);
        app->status_timer_id = 0;
    }
    if (app->worker_thread) {
        worker_cancel(app);
        g_thread_join(app->worker_thread);
        app->worker_thread = NULL;
    }
    /* Cancel any in-flight soup requests so their callbacks don't touch
       the now-destroyed window widgets. */
    g_cancellable_cancel(app->update_cancel);
    app_save_settings(app);
    return FALSE;
}

/* ==========================================================
   Auto update check (once, 3 s after startup)
   ========================================================== */

static gboolean auto_check_update(gpointer data) {
    AppState *app = data;
    if (!app->update_checked) {
        app->update_checked = TRUE;
        updater_check(app, FALSE);
    }
    return G_SOURCE_REMOVE;
}

static void on_theme_selected(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    AppState *app = data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->theme_combo));
    app_apply_theme(app, theme_option_from_index(selected)->id);
}

static void cleanup_legacy_desktop_files(void) {
    const gchar *legacy_desktop[] = {
        "WhisperDrop.desktop",
        "whisperdrop.desktop",
        NULL
    };
    const gchar *legacy_icons[] = {
        "WhisperDrop.png",
        "whisperdrop.png",
        NULL
    };

    gchar *apps_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    for (guint i = 0; legacy_desktop[i]; i++) {
        gchar *path = g_build_filename(apps_dir, legacy_desktop[i], NULL);
        g_unlink(path);
        g_free(path);
    }
    g_free(apps_dir);

    gchar *root_icon_dir = g_build_filename(g_get_user_data_dir(), "icons", NULL);
    for (guint i = 0; legacy_icons[i]; i++) {
        gchar *path = g_build_filename(root_icon_dir, legacy_icons[i], NULL);
        g_unlink(path);
        g_free(path);
    }
    g_free(root_icon_dir);
}

/* ==========================================================
   Desktop integration  (icon + .desktop file for KDE/Wayland)

   On Wayland, window icons come from the .desktop file that matches
   the GApplication app_id — not from gtk_window_set_icon_name().
   We extract the bundled PNG and write a .desktop file to
   ~/.local/share/ on every launch so the icon is always current
   even if the binary has moved.
   ========================================================== */

#ifndef G_OS_WIN32
static void setup_desktop_integration(void) {
    cleanup_legacy_desktop_files();

    /* ---- Extract icon to ~/.local/share/icons/ ---- */
    gchar *icon_dir = g_build_filename(
        g_get_user_data_dir(), "icons", "hicolor", "256x256", "apps", NULL);
    g_mkdir_with_parents(icon_dir, 0755);

    gchar *icon_dest = g_build_filename(
        icon_dir, APP_ICON_NAME ".png", NULL);

    if (!g_file_test(icon_dest, G_FILE_TEST_EXISTS)) {
        GBytes *data = g_resources_lookup_data(
            ICON_RESOURCE, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
        if (data) {
            gsize       len = 0;
            const void *ptr = g_bytes_get_data(data, &len);
            g_file_set_contents(icon_dest, ptr, (gssize)len, NULL);
            g_bytes_unref(data);
        }
    }
    g_free(icon_dest);
    g_free(icon_dir);

    /* ---- Write .desktop file pointing at the running binary ---- */
    char exe_buf[4096] = {0};
    ssize_t exe_len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (exe_len <= 0) return;
    exe_buf[exe_len] = '\0';

    gchar *apps_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    g_mkdir_with_parents(apps_dir, 0755);

    gchar *desktop_dest = g_build_filename(
        apps_dir, APP_DESKTOP_FILE, NULL);

    gchar *contents = g_strdup_printf(
        "[Desktop Entry]\n"
        "Name=WhisperDrop\n"
        "Comment=Drag-and-drop audio transcription with Whisper\n"
        "Exec=%s\n"
        "Icon=%s\n"
        "Type=Application\n"
        "Categories=%s\n"
        "Keywords=AI;Whisper;Audio;Transcription;Notes;\n"
        "StartupWMClass=%s\n"
        "StartupNotify=true\n",
        exe_buf, APP_ICON_NAME, APP_MENU_CATEGORIES, APP_ID);

    g_file_set_contents(desktop_dest, contents, -1, NULL);
    g_free(contents);
    g_free(desktop_dest);

    /* ---- Refresh caches (fire-and-forget, errors silently ignored) ---- */
    gchar *hicolor_dir = g_build_filename(
        g_get_user_data_dir(), "icons", "hicolor", NULL);
    const gchar *cache_argv[] = {
        "gtk-update-icon-cache", "-f", "-t", hicolor_dir, NULL
    };
    g_spawn_async(NULL, (gchar **)cache_argv, NULL,
                  G_SPAWN_SEARCH_PATH |
                  G_SPAWN_STDOUT_TO_DEV_NULL |
                  G_SPAWN_STDERR_TO_DEV_NULL,
                  NULL, NULL, NULL, NULL);

    const gchar *db_argv[] = {
        "update-desktop-database", apps_dir, NULL
    };
    g_spawn_async(NULL, (gchar **)db_argv, NULL,
                  G_SPAWN_SEARCH_PATH |
                  G_SPAWN_STDOUT_TO_DEV_NULL |
                  G_SPAWN_STDERR_TO_DEV_NULL,
                  NULL, NULL, NULL, NULL);

    g_free(hicolor_dir);
    g_free(apps_dir);
}
#endif /* !G_OS_WIN32 */

/* ==========================================================
   UI construction  (app_activate)
   ========================================================== */

void app_activate(GtkApplication *gapp, gpointer user_data) {
    AppState *app = user_data;

    /* Install icon + .desktop file so KDE/Wayland can show the window icon.
       Must happen before gtk_window_present so the compositor sees the files. */
#ifndef G_OS_WIN32
    setup_desktop_integration();
#endif

    /* ---- Actions ---- */
    GSimpleAction *act_about   = g_simple_action_new("about",         NULL);
    GSimpleAction *act_updates = g_simple_action_new("check-updates", NULL);
    g_signal_connect(act_about,   "activate", G_CALLBACK(on_about_activated),         app);
    g_signal_connect(act_updates, "activate", G_CALLBACK(on_check_updates_activated), app);
    g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(act_about));
    g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(act_updates));

    /* ---- Application menu bar ---- */
    GMenu *menubar  = g_menu_new();
    GMenu *help_sub = g_menu_new();
    g_menu_append(help_sub, "Check for Updates\u2026", "app.check-updates");
    g_menu_append(help_sub, "About",                   "app.about");
    g_menu_append_submenu(menubar, "_Help", G_MENU_MODEL(help_sub));
    gtk_application_set_menubar(gapp, G_MENU_MODEL(menubar));
    g_object_unref(help_sub);
    g_object_unref(menubar);

    /* ---- Window ---- */
    GtkWidget *window = gtk_application_window_new(gapp);
    app->window = GTK_APPLICATION_WINDOW(window);
    gtk_window_set_title(GTK_WINDOW(window), APP_NAME " " APP_VERSION);
    gtk_window_set_default_size(GTK_WINDOW(window), 980, 700);
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(window), TRUE);

    /* Register bundled PNG with the icon theme so the window/taskbar icon works */
    GdkDisplay *display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(display,
        GTK_STYLE_PROVIDER(app->theme_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(display);
    gtk_icon_theme_add_resource_path(icon_theme, "/com/saguarosec/whisperdrop/icons");
    gtk_window_set_icon_name(GTK_WINDOW(window), APP_ICON_NAME);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), app);

    GtkWidget *header = gtk_header_bar_new();
    gtk_widget_add_css_class(header, "topbar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), ":minimize,maximize,close");
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title = gtk_label_new(APP_NAME);
    GtkWidget *subtitle = gtk_label_new("Drop audio, queue jobs, and transcribe locally");
    gtk_widget_add_css_class(title, "app-title");
    gtk_widget_add_css_class(subtitle, "app-subtitle");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), subtitle);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);

    app->open_out_btn = gtk_button_new_with_label("Open Output Folder");
    g_signal_connect(app->open_out_btn, "clicked", G_CALLBACK(on_open_out_clicked), app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app->open_out_btn);

    app->stop_btn = gtk_button_new_with_label("Stop");
    gtk_widget_add_css_class(app->stop_btn, "destructive-action");
    gtk_widget_set_sensitive(app->stop_btn, FALSE);
    g_signal_connect(app->stop_btn, "clicked", G_CALLBACK(on_stop_clicked), app);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->stop_btn);

    app->start_btn = gtk_button_new_with_label("Start Transcription");
    gtk_widget_add_css_class(app->start_btn, "suggested-action");
    g_signal_connect(app->start_btn, "clicked", G_CALLBACK(on_start_clicked), app);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->start_btn);

    /* ---- Root VBox ---- */
    GtkWidget *root_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(root_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(root_scroll, TRUE);
    gtk_window_set_child(GTK_WINDOW(window), root_scroll);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(root, "app-shell");
    gtk_widget_set_margin_start (root, 16);
    gtk_widget_set_margin_end   (root, 16);
    gtk_widget_set_margin_top   (root, 16);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(root_scroll), root);

    GtkWidget *main_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_vexpand(main_area, TRUE);
    gtk_box_append(GTK_BOX(root), main_area);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(sidebar, "sidebar-column");
    gtk_widget_set_size_request(sidebar, 360, -1);
    gtk_box_append(GTK_BOX(main_area), sidebar);

    GtkWidget *settings_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(settings_frame, "panel-card");
    gtk_widget_add_css_class(settings_frame, "settings-card");
    gtk_box_append(GTK_BOX(sidebar), settings_frame);

    GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(settings_box, 16);
    gtk_widget_set_margin_end(settings_box, 16);
    gtk_widget_set_margin_top(settings_box, 16);
    gtk_widget_set_margin_bottom(settings_box, 16);
    gtk_frame_set_child(GTK_FRAME(settings_frame), settings_box);

    GtkWidget *settings_title = gtk_label_new("Transcription Settings");
    gtk_widget_add_css_class(settings_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(settings_title), 0.0f);
    gtk_box_append(GTK_BOX(settings_box), settings_title);

    GtkWidget *field = gtk_label_new("Model");
    gtk_label_set_xalign(GTK_LABEL(field), 0.0f);
    gtk_box_append(GTK_BOX(settings_box), field);
    app->model_combo = gtk_combo_box_text_new();
    const gchar *models[] = {
        "turbo","tiny","tiny.en","base","base.en","small","small.en",
        "medium","medium.en","large","large-v2","large-v3", NULL
    };
    for (int i = 0; models[i]; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->model_combo), models[i], models[i]);
    gtk_box_append(GTK_BOX(settings_box), app->model_combo);

    field = gtk_label_new("Output");
    gtk_label_set_xalign(GTK_LABEL(field), 0.0f);
    gtk_box_append(GTK_BOX(settings_box), field);
    app->format_combo = gtk_combo_box_text_new();
    const gchar *fmts[] = {"all","txt","srt","vtt","tsv","json", NULL};
    for (int i = 0; fmts[i]; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->format_combo), fmts[i], fmts[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->format_combo), 1);
    gtk_box_append(GTK_BOX(settings_box), app->format_combo);

    field = gtk_label_new("Theme");
    gtk_label_set_xalign(GTK_LABEL(field), 0.0f);
    gtk_box_append(GTK_BOX(settings_box), field);
    app->theme_combo = gtk_drop_down_new_from_strings((const char * const[]) {
        "System", "Graphite", "Sand", "Forest", NULL
    });
    g_signal_connect(app->theme_combo, "notify::selected",
                     G_CALLBACK(on_theme_selected), app);
    gtk_box_append(GTK_BOX(settings_box), app->theme_combo);

    app->same_folder_check = gtk_check_button_new_with_label("Save next to input file");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->same_folder_check), TRUE);
    g_signal_connect(app->same_folder_check, "toggled", G_CALLBACK(on_same_folder_toggled), app);
    gtk_box_append(GTK_BOX(settings_box), app->same_folder_check);

    field = gtk_label_new("Output folder");
    gtk_label_set_xalign(GTK_LABEL(field), 0.0f);
    gtk_box_append(GTK_BOX(settings_box), field);
    app->out_folder_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->out_folder_entry), "Choose a folder when not saving beside source");
    gtk_widget_set_sensitive(app->out_folder_entry, FALSE);
    gtk_box_append(GTK_BOX(settings_box), app->out_folder_entry);

    app->out_folder_btn = gtk_button_new_with_label("Choose\u2026");
    gtk_widget_set_sensitive(app->out_folder_btn, FALSE);
    g_signal_connect(app->out_folder_btn, "clicked", G_CALLBACK(on_out_folder_clicked), app);
    gtk_box_append(GTK_BOX(settings_box), app->out_folder_btn);

    field = gtk_label_new("Extra whisper args");
    gtk_label_set_xalign(GTK_LABEL(field), 0.0f);
    gtk_box_append(GTK_BOX(settings_box), field);
    app->extra_args_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->extra_args_entry),
        "Optional: e.g., --language en --task transcribe --word_timestamps True");
    gtk_box_append(GTK_BOX(settings_box), app->extra_args_entry);

    app->drop_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(app->drop_frame, "drop-card");
    gtk_box_append(GTK_BOX(sidebar), app->drop_frame);

    GtkWidget *drop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(drop_box, 18);
    gtk_widget_set_margin_end(drop_box, 18);
    gtk_widget_set_margin_top(drop_box, 18);
    gtk_widget_set_margin_bottom(drop_box, 18);
    GtkWidget *drop_title = gtk_label_new("Drop files to begin");
    gtk_widget_add_css_class(drop_title, "drop-title");
    gtk_label_set_xalign(GTK_LABEL(drop_title), 0.0f);
    GtkWidget *drop_label = gtk_label_new(
        "Use drag and drop for journals, interviews, or meeting audio.\n"
        "The queue on the right updates immediately.");
    gtk_label_set_xalign(GTK_LABEL(drop_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(drop_label), TRUE);
    gtk_box_append(GTK_BOX(drop_box), drop_title);
    gtk_box_append(GTK_BOX(drop_box), drop_label);
    gtk_frame_set_child(GTK_FRAME(app->drop_frame), drop_box);

    GtkDropTarget *drop_tgt = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(drop_tgt, "drop", G_CALLBACK(on_drop), app);
    gtk_widget_add_controller(app->drop_frame, GTK_EVENT_CONTROLLER(drop_tgt));

    GtkWidget *workspace = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(workspace, "workspace-column");
    gtk_widget_set_hexpand(workspace, TRUE);
    gtk_widget_set_vexpand(workspace, TRUE);
    gtk_box_append(GTK_BOX(main_area), workspace);

    GtkWidget *queue_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(queue_frame, "panel-card");
    gtk_widget_add_css_class(queue_frame, "queue-card");
    gtk_widget_set_vexpand(queue_frame, TRUE);
    gtk_box_append(GTK_BOX(workspace), queue_frame);

    GtkWidget *queue_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(queue_box, 16);
    gtk_widget_set_margin_end(queue_box, 16);
    gtk_widget_set_margin_top(queue_box, 16);
    gtk_widget_set_margin_bottom(queue_box, 16);
    gtk_frame_set_child(GTK_FRAME(queue_frame), queue_box);

    GtkWidget *queue_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(queue_box), queue_header);
    GtkWidget *queue_title = gtk_label_new("Queued Files");
    gtk_widget_add_css_class(queue_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(queue_title), 0.0f);
    gtk_widget_set_hexpand(queue_title, TRUE);
    gtk_box_append(GTK_BOX(queue_header), queue_title);

    GtkWidget *list_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(queue_header), list_btns);

    app->add_btn = gtk_button_new_with_label("Add Files\u2026");
    gtk_widget_add_css_class(app->add_btn, "suggested-action");
    g_signal_connect(app->add_btn, "clicked", G_CALLBACK(on_add_files_clicked), app);
    gtk_box_append(GTK_BOX(list_btns), app->add_btn);

    app->remove_btn = gtk_button_new_with_label("Remove Selected");
    g_signal_connect(app->remove_btn, "clicked", G_CALLBACK(on_remove_clicked), app);
    gtk_box_append(GTK_BOX(list_btns), app->remove_btn);

    app->clear_btn = gtk_button_new_with_label("Clear");
    g_signal_connect(app->clear_btn, "clicked", G_CALLBACK(on_clear_clicked), app);
    gtk_box_append(GTK_BOX(list_btns), app->clear_btn);

    GtkWidget *list_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(list_scroll, TRUE);
    gtk_box_append(GTK_BOX(queue_box), list_scroll);

    app->file_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_add_css_class(GTK_WIDGET(app->file_list), "queue-list");
    gtk_list_box_set_selection_mode(app->file_list, GTK_SELECTION_MULTIPLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll),
                                  GTK_WIDGET(app->file_list));

    GtkWidget *activity_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(activity_frame, "panel-card");
    gtk_widget_add_css_class(activity_frame, "activity-card");
    gtk_widget_set_vexpand(activity_frame, TRUE);
    gtk_box_append(GTK_BOX(workspace), activity_frame);

    GtkWidget *activity_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(activity_box, 16);
    gtk_widget_set_margin_end(activity_box, 16);
    gtk_widget_set_margin_top(activity_box, 16);
    gtk_widget_set_margin_bottom(activity_box, 16);
    gtk_frame_set_child(GTK_FRAME(activity_frame), activity_box);

    GtkWidget *activity_title = gtk_label_new("Live Activity");
    gtk_widget_add_css_class(activity_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(activity_title), 0.0f);
    gtk_box_append(GTK_BOX(activity_box), activity_title);

    app->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_box_append(GTK_BOX(activity_box), GTK_WIDGET(app->progress));

    GtkWidget *log_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(log_scroll, TRUE);
    gtk_box_append(GTK_BOX(activity_box), log_scroll);

    app->log_view = gtk_text_view_new();
    app->log_buf  = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->log_view));
    gtk_text_view_set_editable    (GTK_TEXT_VIEW(app->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->log_view), FALSE);
    gtk_text_view_set_wrap_mode   (GTK_TEXT_VIEW(app->log_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace   (GTK_TEXT_VIEW(app->log_view), TRUE);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW(log_scroll), app->log_view);

    /* ---- Status label ---- */
    app->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0f);
    gtk_widget_add_css_class(app->status_label, "status-pill");
    gtk_box_append(GTK_BOX(root), app->status_label);

    /* ---- Load settings and show ---- */
    app_load_settings(app);
    gtk_window_present(GTK_WINDOW(window));

    g_timeout_add_seconds(3, auto_check_update, app);
}

/* installer/whisperdrop-uninstaller.c
 * WhisperDrop graphical uninstaller — C / GTK 4
 * SPDX-License-Identifier: MIT
 *
 * Build:  ninja -C build   (via installer/meson.build + root meson.build)
 * Run:    ./build/installer/whisperdrop-uninstaller
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <unistd.h>

/* ─── Embedded icon (generated at build time by embed_binary.py) ─────────── */

extern const unsigned char whisperdrop_icon[];
extern const size_t        whisperdrop_icon_len;

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define WIN_W 520
#define WIN_H 380

/* ─── Forward declarations ───────────────────────────────────────────────── */

typedef struct Uninst Uninst;

static void navigate_un(Uninst *s, int page);
static void start_uninstall(Uninst *s);

/* ─── State struct ───────────────────────────────────────────────────────── */

struct Uninst {
    GtkApplication *gapp;
    GtkWindow      *window;
    GtkStack       *stack;
    GtkLabel       *hdr_title;

    GtkCheckButton *rm_settings_cb;  /* "Also remove saved settings" */
    GtkButton      *btn_uninstall;   /* red confirm button           */
    GtkButton      *btn_cancel;      /* on confirm page              */
    GtkButton      *btn_close;       /* on progress page (post-done) */

    GtkProgressBar *prog;
    GtkTextBuffer  *log_buf;
    GtkTextView    *log_view;

    int    cur;      /* 0=confirm, 1=progress */
    gchar *src_ico;  /* path to WhisperDrop.png, may be NULL */
};

static const char *PG_NAME[2]  = { "confirm", "progress" };
static const char *PG_TITLE[2] = {
    "Remove WhisperDrop",
    "Removing WhisperDrop\xe2\x80\xa6"
};

/* ─── Icon helpers (shared with installer) ───────────────────────────────── */

static gchar *get_exe_dir(void)
{
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return g_path_get_dirname(buf); }
    return g_get_current_dir();
}

static gchar *find_icon(const gchar *dir)
{
    const char *rel[] = {
        "WhisperDrop.png",
        "../WhisperDrop.png",
        "../../WhisperDrop.png",
        "../data/WhisperDrop.png",
        "../../data/WhisperDrop.png",
        NULL
    };
    for (int i = 0; rel[i]; i++) {
        gchar *p = g_build_filename(dir, rel[i], NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
        g_free(p);
    }
    gchar *p = g_build_filename(g_get_home_dir(), ".local", "share",
                                "icons", "WhisperDrop.png", NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
    g_free(p);
    return NULL;
}

/* ─── Embedded icon loader ───────────────────────────────────────────────── */

static GtkWidget *make_logo_from_embedded(int pixel_size)
{
    GBytes     *bytes = g_bytes_new_static(whisperdrop_icon, whisperdrop_icon_len);
    GError     *err   = NULL;
    GdkTexture *tex   = gdk_texture_new_from_bytes(bytes, &err);
    g_bytes_unref(bytes);
    if (!tex) { g_clear_error(&err); return NULL; }
    GtkWidget *img = gtk_image_new_from_paintable(GDK_PAINTABLE(tex));
    g_object_unref(tex);
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixel_size);
    return img;
}

/* ─── Log helpers (main-thread idle callbacks) ───────────────────────────── */

typedef struct { Uninst *s; gchar *line; double frac; } ULogMsg;
typedef struct { Uninst *s; gboolean ok; }               UDoneMsg;

static gboolean ulog_cb(gpointer data)
{
    ULogMsg *m = data;
    if (m->line && m->line[0]) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(m->s->log_buf, &end);
        gtk_text_buffer_insert(m->s->log_buf, &end, m->line, -1);
        gtk_text_buffer_insert(m->s->log_buf, &end, "\n", -1);
        gtk_text_buffer_get_end_iter(m->s->log_buf, &end);
        GtkTextMark *ins = gtk_text_buffer_get_insert(m->s->log_buf);
        gtk_text_buffer_move_mark(m->s->log_buf, ins, &end);
        gtk_text_view_scroll_to_mark(m->s->log_view, ins, 0.0, TRUE, 0.0, 1.0);
    }
    if (m->frac >= 0)
        gtk_progress_bar_set_fraction(m->s->prog, m->frac);
    g_free(m->line);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static gboolean udone_cb(gpointer data)
{
    UDoneMsg *m = data;
    Uninst *s = m->s;
    if (m->ok) {
        gchar *mu = g_markup_printf_escaped("<b>%s</b>", "WhisperDrop Removed");
        gtk_label_set_markup(s->hdr_title, mu);
        g_free(mu);
    }
    gtk_widget_set_sensitive(GTK_WIDGET(s->btn_close), TRUE);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void emit_ulog(Uninst *s, const char *text, double frac)
{
    ULogMsg *m = g_new0(ULogMsg, 1);
    m->s    = s;
    m->line = g_strdup(text);
    m->frac = frac;
    g_idle_add(ulog_cb, m);
}

/* ─── Navigation ─────────────────────────────────────────────────────────── */

static void navigate_un(Uninst *s, int page)
{
    s->cur = page;
    gtk_stack_set_visible_child_name(s->stack, PG_NAME[page]);
    gchar *mu = g_markup_printf_escaped("<b>%s</b>", PG_TITLE[page]);
    gtk_label_set_markup(s->hdr_title, mu);
    g_free(mu);
    if (page == 1) start_uninstall(s);
}

static void on_uninstall_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    navigate_un(ud, 1);
}

static void on_cancel_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    Uninst *s = ud;
    gtk_window_destroy(s->window);
}

static void on_close_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    Uninst *s = ud;
    gtk_window_destroy(s->window);
}

/* ─── Uninstall thread ───────────────────────────────────────────────────── */

/*
 * Remove a single file or directory (recursively if dir).
 * Returns TRUE if it no longer exists (either deleted or never existed).
 */
static gboolean remove_path(const gchar *path, gboolean recursive)
{
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) return TRUE;

    GFile  *f   = g_file_new_for_path(path);
    GError *err = NULL;

    gboolean ok;
    if (recursive) {
        /* GFile does not provide a recursive delete in older GLib; iterate */
        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            GFileEnumerator *en = g_file_enumerate_children(f,
                G_FILE_ATTRIBUTE_STANDARD_NAME,
                G_FILE_QUERY_INFO_NONE, NULL, NULL);
            if (en) {
                GFileInfo *info;
                while ((info = g_file_enumerator_next_file(en, NULL, NULL))) {
                    const char *name = g_file_info_get_name(info);
                    gchar *child_path = g_build_filename(path, name, NULL);
                    remove_path(child_path, TRUE);
                    g_free(child_path);
                    g_object_unref(info);
                }
                g_file_enumerator_close(en, NULL, NULL);
                g_object_unref(en);
            }
        }
    }

    ok = g_file_delete(f, NULL, &err);
    if (!ok && err) g_clear_error(&err);
    g_object_unref(f);
    return ok || !g_file_test(path, G_FILE_TEST_EXISTS);
}

static void uninstall_thread_fn(GTask *task, gpointer src_obj, gpointer td,
                                 GCancellable *cancel)
{
    (void)src_obj; (void)cancel;
    Uninst *s = td;
    int step = 0;
    int total = gtk_check_button_get_active(s->rm_settings_cb) ? 5 : 3;

    emit_ulog(s, "Removing WhisperDrop\xe2\x80\xa6", 0.0);

    /* 1. Application binary */
    gchar *bin = g_build_filename(g_get_home_dir(), ".local", "bin",
                                  "WhisperDrop", NULL);
    emit_ulog(s, "Removing application\xe2\x80\xa6",
              (double)step / total);
    remove_path(bin, FALSE);
    g_free(bin);
    step++;

    /* 2. .desktop shortcut */
    gchar *dt = g_build_filename(g_get_home_dir(), ".local", "share",
                                 "applications", "WhisperDrop.desktop", NULL);
    emit_ulog(s, "Removing application shortcut\xe2\x80\xa6",
              (double)step / total);
    remove_path(dt, FALSE);
    g_free(dt);
    step++;

    /* Refresh the application database */
    gchar *db_dir = g_build_filename(g_get_home_dir(), ".local",
                                     "share", "applications", NULL);
    gchar *upd = g_strdup_printf("update-desktop-database %s", db_dir);
    g_free(db_dir);
    g_spawn_command_line_async(upd, NULL);
    g_free(upd);

    /* 3. Application icon */
    gchar *ico = g_build_filename(g_get_home_dir(), ".local", "share",
                                  "icons", "WhisperDrop.png", NULL);
    emit_ulog(s, "Removing application icon\xe2\x80\xa6",
              (double)step / total);
    remove_path(ico, FALSE);
    g_free(ico);
    step++;

    /* 4 & 5. Saved settings and data (optional) */
    if (gtk_check_button_get_active(s->rm_settings_cb)) {
        gchar *cfg = g_build_filename(g_get_user_config_dir(),
                                      "WhisperDrop", NULL);
        emit_ulog(s, "Removing saved settings\xe2\x80\xa6",
                  (double)step / total);
        remove_path(cfg, TRUE);
        g_free(cfg);
        step++;

        gchar *data = g_build_filename(g_get_user_data_dir(),
                                       "whisperdrop", NULL);
        emit_ulog(s, "Removing saved data\xe2\x80\xa6",
                  (double)step / total);
        remove_path(data, TRUE);
        g_free(data);
        step++;
    }

    emit_ulog(s, "WhisperDrop has been removed.", 1.0);

    UDoneMsg *dm = g_new0(UDoneMsg, 1);
    dm->s  = s;
    dm->ok = TRUE;
    g_idle_add(udone_cb, dm);
    g_task_return_boolean(task, TRUE);
}

static void start_uninstall(Uninst *s)
{
    GTask *t = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(t, s, NULL);
    g_task_run_in_thread(t, uninstall_thread_fn);
    g_object_unref(t);
}

/* ─── Page builders ──────────────────────────────────────────────────────── */

static GtkWidget *build_confirm(Uninst *s)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(box, TRUE);

    GtkWidget *info = gtk_label_new(
        "The following will be removed from your account:\n\n"
        "  \xe2\x80\xa2  WhisperDrop application  "
              "(~/.local/bin/WhisperDrop)\n"
        "  \xe2\x80\xa2  Applications menu shortcut  "
              "(~/.local/share/applications/WhisperDrop.desktop)\n"
        "  \xe2\x80\xa2  Application icon  "
              "(~/.local/share/icons/WhisperDrop.png)");
    gtk_label_set_wrap(GTK_LABEL(info), TRUE);
    gtk_label_set_xalign(GTK_LABEL(info), 0.0f);
    gtk_box_append(GTK_BOX(box), info);

    GtkWidget *cb = gtk_check_button_new_with_label(
        "Also remove saved settings and data "
        "(~/.config/WhisperDrop/ and ~/.local/share/whisperdrop/)");
    s->rm_settings_cb = GTK_CHECK_BUTTON(cb);
    gtk_label_set_wrap(
        GTK_LABEL(gtk_check_button_get_child(GTK_CHECK_BUTTON(cb))), TRUE);
    gtk_box_append(GTK_BOX(box), cb);

    /* Button row */
    GtkWidget *btnrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(btnrow, 8);

    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    s->btn_cancel = GTK_BUTTON(cancel);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel_clicked), s);
    gtk_box_append(GTK_BOX(btnrow), cancel);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(btnrow), spacer);

    GtkWidget *uninstall = gtk_button_new_with_label("Uninstall");
    s->btn_uninstall = GTK_BUTTON(uninstall);
    gtk_widget_add_css_class(uninstall, "destructive-action");
    g_signal_connect(uninstall, "clicked",
        G_CALLBACK(on_uninstall_clicked), s);
    gtk_box_append(GTK_BOX(btnrow), uninstall);

    gtk_box_append(GTK_BOX(box), btnrow);

    return box;
}

static GtkWidget *build_progress(Uninst *s)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);

    /* Progress bar */
    GtkWidget *prog = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(prog), FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(prog), 0.0);
    s->prog = GTK_PROGRESS_BAR(prog);
    gtk_box_append(GTK_BOX(box), prog);

    /* Log */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
    s->log_buf  = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    s->log_view = GTK_TEXT_VIEW(view);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_box_append(GTK_BOX(box), scroll);

    /* Close button — insensitive until done */
    GtkWidget *btnrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(btnrow, 4);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(btnrow), spacer);

    GtkWidget *close = gtk_button_new_with_label("Close");
    s->btn_close = GTK_BUTTON(close);
    gtk_widget_set_sensitive(close, FALSE);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), s);
    gtk_box_append(GTK_BOX(btnrow), close);

    gtk_box_append(GTK_BOX(box), btnrow);

    return box;
}

/* ─── Window construction ────────────────────────────────────────────────── */

static void build_uninst_window(Uninst *s)
{
    GtkWidget *win = gtk_application_window_new(s->gapp);
    s->window = GTK_WINDOW(win);
    gtk_window_set_title(GTK_WINDOW(win), "WhisperDrop Uninstaller");
    gtk_window_set_default_size(GTK_WINDOW(win), WIN_W, WIN_H);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), outer);

    /* Header */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(hdr, 16);
    gtk_widget_set_margin_end(hdr, 16);
    gtk_widget_set_margin_top(hdr, 10);
    gtk_widget_set_margin_bottom(hdr, 10);

    GtkWidget *logo = NULL;
    if (s->src_ico)
        logo = gtk_image_new_from_file(s->src_ico);
    if (!logo)
        logo = make_logo_from_embedded(64);
    if (!logo) {
        logo = gtk_image_new_from_icon_name("edit-delete");
        gtk_image_set_pixel_size(GTK_IMAGE(logo), 64);
    }
    gtk_widget_set_halign(logo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(hdr), logo);

    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_lbl), "<b>Remove WhisperDrop</b>");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_CENTER);
    s->hdr_title = GTK_LABEL(title_lbl);
    gtk_box_append(GTK_BOX(hdr), title_lbl);

    gtk_box_append(GTK_BOX(outer), hdr);
    gtk_box_append(GTK_BOX(outer),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Stack */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),
        GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 200);
    gtk_widget_set_vexpand(stack, TRUE);
    s->stack = GTK_STACK(stack);

    gtk_stack_add_named(GTK_STACK(stack), build_confirm(s),  PG_NAME[0]);
    gtk_stack_add_named(GTK_STACK(stack), build_progress(s), PG_NAME[1]);

    gtk_box_append(GTK_BOX(outer), stack);

    /* Start on the confirm page */
    s->cur = 0;
    gtk_stack_set_visible_child_name(s->stack, PG_NAME[0]);

    gtk_window_present(GTK_WINDOW(win));
}

/* ─── Application entry point ────────────────────────────────────────────── */

static void on_activate(GtkApplication *gapp, gpointer ud)
{
    Uninst *s = ud;
    s->gapp   = gapp;
    build_uninst_window(s);
}

int main(int argc, char **argv)
{
    gchar *exe_dir = get_exe_dir();

    Uninst *s  = g_new0(Uninst, 1);
    s->src_ico = find_icon(exe_dir);
    g_free(exe_dir);

    GtkApplication *app = gtk_application_new(
        "com.saguarosec.WhisperDropUninstaller",
        G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), s);

    int ret = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    g_free(s->src_ico);
    g_free(s);
    return ret;
}

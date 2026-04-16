/* installer/whisperdrop-installer.c
 * WhisperDrop graphical installer — C / GTK 4
 * SPDX-License-Identifier: MIT
 *
 * Build:  ninja -C build   (via installer/meson.build + root meson.build)
 * Run:    ./build/installer/whisperdrop-installer
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif
#endif

/* ─── Embedded assets (generated at build time by embed_binary.py) ───────── */

extern const unsigned char whisperdrop_binary[];
extern const size_t        whisperdrop_binary_len;
extern const unsigned char whisperdrop_icon[];
extern const size_t        whisperdrop_icon_len;

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define WIN_W  620
#define WIN_H  480

typedef enum { DEP_PENDING = 0, DEP_OK, DEP_MISSING } DepSt;

#ifdef G_OS_WIN32
/* Windows: check Python, FFmpeg, and Whisper only.
   GTK4 and its friends are bundled with the MSYS2 runtime. */
#define N_DEPS 3
static const struct { const char *label; const char *exe; } DEPS[N_DEPS] = {
    { "Python interpreter",             "python"  },
    { "Audio decoder (FFmpeg)",         "ffmpeg"  },
    { "Transcription engine (Whisper)", "whisper" },
};
#else
/* Linux: check FFmpeg, Whisper, GTK4, libsoup3, json-glib */
#define N_DEPS 5
typedef enum {
    PKG_UNKNOWN = 0,
    PKG_APT,      /* Debian / Ubuntu / Mint            */
    PKG_DNF,      /* Fedora / RHEL / CentOS / Rocky    */
    PKG_PACMAN,   /* Arch / Manjaro / CachyOS           */
    PKG_ZYPPER    /* openSUSE                           */
} PkgMgr;
#endif

/* ─── Forward declarations ───────────────────────────────────────────────── */

typedef struct Inst Inst;

static void  navigate      (Inst *s, int page);
static void  update_buttons(Inst *s);
static void  start_dep_check(Inst *s);
static void  start_install  (Inst *s);
static void  show_error     (GtkWindow *p, const char *title, const char *body);
#ifndef G_OS_WIN32
static void  show_manual_install(GtkWindow *p);
#endif

/* ─── State struct ───────────────────────────────────────────────────────── */

struct Inst {
    GtkApplication *gapp;
    GtkWindow      *window;
    GtkStack       *stack;
    GtkLabel       *hdr_title;

    GtkButton *btn_back;
    GtkButton *btn_next;   /* "Next" / "Install" / "Close" */
    GtkButton *btn_launch; /* visible only on finish page  */
    int        cur;        /* 0=welcome … 4=finish         */

    /* License page */
    GtkCheckButton *lic_cb;

    /* Dep-check page */
    GtkSpinner *dep_spin[N_DEPS];
    GtkLabel   *dep_icon[N_DEPS];
    GtkLabel   *dep_sum;
    DepSt       dep_st[N_DEPS];
    gboolean    dep_done;

    /* Install page */
    GtkProgressBar *inst_prog;
    GtkTextBuffer  *inst_buf;
    GtkTextView    *inst_view;

    /* Runtime */
#ifndef G_OS_WIN32
    PkgMgr  pkgmgr;
#endif
    gchar  *src_bin;   /* path to WhisperDrop binary  */
    gchar  *src_ico;   /* path to WhisperDrop.png     */
};

/* ─── Static metadata ────────────────────────────────────────────────────── */

static const char *PG_NAME[5] = {
    "welcome", "license", "depcheck", "install", "finish"
};
static const char *PG_TITLE[5] = {
    "Welcome to WhisperDrop",
    "License Agreement",
    "Checking Your System",
    "Installing WhisperDrop",
    "You\xe2\x80\x99re All Set!"   /* UTF-8 right single quotation mark */
};

static const char MIT_LICENSE[] =
    "MIT License\n\n"
    "Copyright (c) 2020 WhisperDrop by Michael Galde\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining "
    "a copy of this software and associated documentation files (the "
    "\"Software\"), to deal in the Software without restriction, including "
    "without limitation the rights to use, copy, modify, merge, publish, "
    "distribute, sublicense, and/or sell copies of the Software, and to "
    "permit persons to whom the Software is furnished to do so, subject to "
    "the following conditions:\n\n"
    "The above copyright notice and this permission notice shall be "
    "included in all copies or substantial portions of the Software.\n\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
    "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. "
    "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY "
    "CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
    "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE "
    "SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n"
    "---\n\n"
    "If you read this far and like this software, feel free to reach out to\n"
    "WhisperDrop@saguarosec.com \342\200\224 we\342\200\231d love to hear from you!";

#ifndef G_OS_WIN32
static const struct {
    const char *label;
    const char *exe;   /* non-NULL → check with g_find_program_in_path */
    const char *lib;   /* non-NULL → check library file existence       */
} DEPS[N_DEPS] = {
    { "Audio decoder (FFmpeg)",         "ffmpeg",  NULL                    },
    { "Transcription engine (Whisper)", "whisper", NULL                    },
    { "Graphics library (GTK4)",        NULL,      "libgtk-4.so.1"         },
    { "Network library (libsoup3)",     NULL,      "libsoup-3.0.so.0"      },
    { "Data library (json-glib)",       NULL,      "libjson-glib-1.0.so.0" },
};

/*
 * Package names indexed by [dep][pkgmgr-1]:
 *   col 0 = APT, col 1 = DNF, col 2 = PACMAN, col 3 = ZYPPER
 */
static const char *PKG_NAMES[N_DEPS][4] = {
    /* ffmpeg  */ { "ffmpeg",              "ffmpeg",    "ffmpeg",   "ffmpeg"              },
    /* whisper */ { NULL,                  NULL,         NULL,       NULL                  },
    /* gtk4    */ { "libgtk-4-1",          "gtk4",       "gtk4",     "libgtk-4-1"          },
    /* soup3   */ { "libsoup-3.0-0",       "libsoup3",   "libsoup3", "libsoup3-0"          },
    /* json-gl */ { "libjson-glib-1.0-0",  "json-glib",  "json-glib","libjson-glib-1_0-0"  },
};
#endif /* !G_OS_WIN32 */

/* ─── Utility helpers ────────────────────────────────────────────────────── */

#ifndef G_OS_WIN32
static gboolean lib_exists(const char *libname)
{
    static const char *dirs[] = {
        "/usr/lib",
        "/usr/lib64",
        "/usr/lib/x86_64-linux-gnu",
        "/usr/lib/aarch64-linux-gnu",
        NULL
    };
    for (int i = 0; dirs[i]; i++) {
        gchar *p = g_build_filename(dirs[i], libname, NULL);
        gboolean ok = g_file_test(p, G_FILE_TEST_EXISTS);
        g_free(p);
        if (ok) return TRUE;
    }
    return FALSE;
}

static PkgMgr detect_pkg(void)
{
    gchar *raw = NULL;
    if (!g_file_get_contents("/etc/os-release", &raw, NULL, NULL))
        return PKG_UNKNOWN;

    /* GKeyFile requires a [group] header; prepend one */
    gchar *wrapped = g_strdup_printf("[os]\n%s", raw);
    g_free(raw);

    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_data(kf, wrapped, -1, G_KEY_FILE_NONE, NULL)) {
        g_free(wrapped);
        g_key_file_free(kf);
        return PKG_UNKNOWN;
    }
    g_free(wrapped);

    gchar *id   = g_key_file_get_string(kf, "os", "ID",      NULL);
    gchar *like = g_key_file_get_string(kf, "os", "ID_LIKE", NULL);
    g_key_file_free(kf);

    PkgMgr r = PKG_UNKNOWN;
    const char *v[3] = { id ? id : "", like ? like : "", NULL };

    for (int i = 0; v[i] && r == PKG_UNKNOWN; i++) {
        if (strstr(v[i], "arch")   || strstr(v[i], "manjaro") ||
            strstr(v[i], "cachyos")|| strstr(v[i], "endeavouros"))
            r = PKG_PACMAN;
        else if (strstr(v[i], "debian") || strstr(v[i], "ubuntu") ||
                 strstr(v[i], "mint"))
            r = PKG_APT;
        else if (strstr(v[i], "fedora") || strstr(v[i], "rhel") ||
                 strstr(v[i], "centos") || strstr(v[i], "rocky"))
            r = PKG_DNF;
        else if (strstr(v[i], "opensuse") || strstr(v[i], "suse"))
            r = PKG_ZYPPER;
    }
    g_free(id);
    g_free(like);
    return r;
}
#endif /* !G_OS_WIN32 */

/* Return the directory containing this executable (caller must g_free). */
static gchar *get_exe_dir(void)
{
#ifdef G_OS_WIN32
    wchar_t wbuf[4096] = {0};
    char    buf[4096]  = {0};
    GetModuleFileNameW(NULL, wbuf, G_N_ELEMENTS(wbuf) - 1);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf), NULL, NULL);
    if (buf[0]) return g_path_get_dirname(buf);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        return g_path_get_dirname(buf);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return g_path_get_dirname(buf);
    }
#endif
    return g_get_current_dir();
}

static gchar *find_binary(const gchar *dir)
{
    const char *rel[] = {
        "WhisperDrop",
        "../WhisperDrop",
        "../build/WhisperDrop",
        "../../build/WhisperDrop",
        NULL
    };
    for (int i = 0; rel[i]; i++) {
        gchar *p = g_build_filename(dir, rel[i], NULL);
        if (g_file_test(p, G_FILE_TEST_IS_REGULAR)) return p;
        g_free(p);
    }
    return NULL;
}

static gchar *find_icon(const gchar *dir)
{
    const char *rel[] = {
        "WhisperDrop.png",
        "../WhisperDrop.png",
        "../../WhisperDrop.png",       /* project root when run from build/installer/ */
        "../data/WhisperDrop.png",
        "../../data/WhisperDrop.png",
        NULL
    };
    for (int i = 0; rel[i]; i++) {
        gchar *p = g_build_filename(dir, rel[i], NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
        g_free(p);
    }
    /* Already installed in the user's icon directory */
    gchar *p = g_build_filename(g_get_home_dir(), ".local", "share",
                                "icons", "WhisperDrop.png", NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
    g_free(p);
    return NULL;
}

/* ─── Embedded asset helpers ─────────────────────────────────────────────── */

/*
 * Write the embedded WhisperDrop binary to a temporary file.
 * Returns the path (caller must g_free + g_unlink when done),
 * or NULL on failure.
 */
static gchar *extract_embedded_binary(void)
{
    GError *err  = NULL;
    gchar  *path = NULL;
    gint    fd   = g_file_open_tmp("whisperdrop-extract-XXXXXX", &path, &err);
    if (fd < 0) {
        g_clear_error(&err);
        return NULL;
    }
    gsize written = 0;
    while (written < whisperdrop_binary_len) {
        gssize n = write(fd, whisperdrop_binary + written,
                         whisperdrop_binary_len - written);
        if (n <= 0) break;
        written += (gsize)n;
    }
    close(fd);
    if (written != whisperdrop_binary_len) {
        g_unlink(path);
        g_free(path);
        return NULL;
    }
    return path;
}

/*
 * Load the embedded PNG icon from memory into a GtkWidget (GtkImage).
 * Returns NULL if GDK can't decode it (caller falls back to theme icon).
 */
static GtkWidget *make_logo_from_embedded(int pixel_size)
{
    GBytes *bytes = g_bytes_new_static(whisperdrop_icon, whisperdrop_icon_len);
    GError *err   = NULL;
    GdkTexture *tex = gdk_texture_new_from_bytes(bytes, &err);
    g_bytes_unref(bytes);
    if (!tex) {
        g_clear_error(&err);
        return NULL;
    }
    GtkWidget *img = gtk_image_new_from_paintable(GDK_PAINTABLE(tex));
    g_object_unref(tex);
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixel_size);
    return img;
}

/* ─── Dialog helpers ─────────────────────────────────────────────────────── */

static void dlg_destroy_on_response(GtkDialog *d, int r, gpointer ud)
{
    (void)r; (void)ud;
    gtk_window_destroy(GTK_WINDOW(d));
}

static void show_error(GtkWindow *parent, const char *title, const char *body)
{
    GtkWidget *d = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", body);
    g_signal_connect(d, "response", G_CALLBACK(dlg_destroy_on_response), NULL);
    gtk_window_present(GTK_WINDOW(d));
}

#ifndef G_OS_WIN32
static void on_manual_response(GtkDialog *d, int r, gpointer ud)
{
    (void)ud;
    if (r == 1)
        g_app_info_launch_default_for_uri(
            "https://github.com/mgalde/WhisperDrop#readme", NULL, NULL);
    gtk_window_destroy(GTK_WINDOW(d));
}

static void show_manual_install(GtkWindow *parent)
{
    GtkWidget *d = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
        "Manual Setup Required");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
        "Your Linux distribution wasn\xe2\x80\x99t recognized, so the installer "
        "can\xe2\x80\x99t set up the required software automatically.\n\n"
        "Please install the following using your system\xe2\x80\x99s package manager:\n"
        "  \xe2\x80\xa2  FFmpeg\n"
        "  \xe2\x80\xa2  GTK 4\n"
        "  \xe2\x80\xa2  libsoup 3\n"
        "  \xe2\x80\xa2  json-glib\n\n"
        "Then install Whisper by running:\n"
        "    pip install -U openai-whisper\n\n"
        "The Setup Guide has step-by-step instructions for common distributions.");
    gtk_dialog_add_button(GTK_DIALOG(d), "View Setup Guide", 1);
    gtk_dialog_add_button(GTK_DIALOG(d), "Close", GTK_RESPONSE_CLOSE);
    g_signal_connect(d, "response", G_CALLBACK(on_manual_response), NULL);
    gtk_window_present(GTK_WINDOW(d));
}
#endif /* !G_OS_WIN32 (show_manual_install) */

/* ─── Navigation ─────────────────────────────────────────────────────────── */

static void update_buttons(Inst *s)
{
    int p = s->cur;

    /* Back: visible on pages 1 and 2 only */
    gtk_widget_set_visible(GTK_WIDGET(s->btn_back), p == 1 || p == 2);

    /* Launch: only on the finish page */
    gtk_widget_set_visible(GTK_WIDGET(s->btn_launch), p == 4);

    /* Next label adapts per page */
    if (p == 2)
        gtk_button_set_label(s->btn_next, "Install");
    else if (p == 4)
        gtk_button_set_label(s->btn_next, "Close");
    else
        gtk_button_set_label(s->btn_next, "Next");

    /* Next sensitivity */
    gboolean ok = TRUE;
    if (p == 1)      /* license: need checkbox */
        ok = gtk_check_button_get_active(s->lic_cb);
    else if (p == 2) /* dep-check: wait for completion */
        ok = s->dep_done;
    else if (p == 3) /* install: no manual advance */
        ok = FALSE;

    gtk_widget_set_sensitive(GTK_WIDGET(s->btn_next), ok);
}

static void navigate(Inst *s, int page)
{
    s->cur = page;
    gtk_stack_set_visible_child_name(s->stack, PG_NAME[page]);

    gchar *mu = g_markup_printf_escaped("<b>%s</b>", PG_TITLE[page]);
    gtk_label_set_markup(s->hdr_title, mu);
    g_free(mu);

    update_buttons(s);

    if (page == 2) start_dep_check(s);
    if (page == 3) start_install(s);
}

static void on_back(GtkButton *b, gpointer ud)
{
    (void)b;
    Inst *s = ud;
    if (s->cur > 0) navigate(s, s->cur - 1);
}

static void on_next(GtkButton *b, gpointer ud)
{
    (void)b;
    Inst *s = ud;
    if (s->cur == 4)
        gtk_window_destroy(s->window);
    else
        navigate(s, s->cur + 1);
}

static void on_launch(GtkButton *b, gpointer ud)
{
    (void)b;
    Inst *s = ud;
#ifdef G_OS_WIN32
    gchar *bin = g_build_filename(g_get_user_data_dir(), "WhisperDrop",
                                  "WhisperDrop.exe", NULL);
#else
    gchar *bin = g_build_filename(g_get_home_dir(), ".local", "bin",
                                  "WhisperDrop", NULL);
#endif
    GError *e = NULL;
    if (!g_spawn_command_line_async(bin, &e)) {
        show_error(s->window, "Could not launch WhisperDrop",
            e ? e->message
              : "The application file could not be started. "
                "Try relaunching it from your Applications menu.");
        g_clear_error(&e);
    }
    g_free(bin);
}

/* ─── Dep-check async ────────────────────────────────────────────────────── */

typedef struct { Inst *s; int idx; DepSt st; }       DepRowMsg;
typedef struct { Inst *s; gboolean any_missing; }     DepDoneMsg;

static gboolean dep_row_cb(gpointer data)
{
    DepRowMsg *m = data;
    gtk_spinner_stop(m->s->dep_spin[m->idx]);
    gtk_widget_set_visible(GTK_WIDGET(m->s->dep_spin[m->idx]), FALSE);

    if (m->st == DEP_OK)
        gtk_label_set_markup(m->s->dep_icon[m->idx],
            "<span foreground=\"#2ecc71\">\xe2\x9c\x94</span>");   /* ✔ */
    else
        gtk_label_set_markup(m->s->dep_icon[m->idx],
            "<span foreground=\"#e74c3c\">\xe2\x9c\x98</span>");   /* ✘ */

    gtk_widget_set_visible(GTK_WIDGET(m->s->dep_icon[m->idx]), TRUE);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static gboolean dep_done_cb(gpointer data)
{
    DepDoneMsg *m = data;
    Inst *s = m->s;

    if (m->any_missing)
        gtk_label_set_markup(s->dep_sum,
            "<span foreground=\"#e67e22\">"
            "Some items need to be installed. "
            "Click \xe2\x80\x98Install\xe2\x80\x99 to continue."
            "</span>");
    else
        gtk_label_set_markup(s->dep_sum,
            "<span foreground=\"#2ecc71\">"
            "\xe2\x9c\x94 Everything is ready. "
            "Click \xe2\x80\x98Install\xe2\x80\x99 to continue."
            "</span>");

    s->dep_done = TRUE;
    gtk_widget_set_sensitive(GTK_WIDGET(s->btn_back), TRUE);
    update_buttons(s);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void dep_thread_fn(GTask *task, gpointer src_obj, gpointer td,
                           GCancellable *cancel)
{
    (void)src_obj; (void)cancel;
    Inst *s = td;
    gboolean any = FALSE;

    for (int i = 0; i < N_DEPS; i++) {
        DepSt st;
#ifdef G_OS_WIN32
        /* On Windows, try both "python" and "python3" for the interpreter */
        gchar *p = g_find_program_in_path(DEPS[i].exe);
        if (!p && i == 0)
            p = g_find_program_in_path("python3");
        st = p ? DEP_OK : DEP_MISSING;
        g_free(p);
#else
        if (DEPS[i].exe) {
            gchar *p = g_find_program_in_path(DEPS[i].exe);
            st = p ? DEP_OK : DEP_MISSING;
            g_free(p);
        } else {
            st = lib_exists(DEPS[i].lib) ? DEP_OK : DEP_MISSING;
        }
#endif
        s->dep_st[i] = st;
        if (st == DEP_MISSING) any = TRUE;

        DepRowMsg *rm = g_new0(DepRowMsg, 1);
        rm->s = s; rm->idx = i; rm->st = st;
        g_idle_add(dep_row_cb, rm);
    }

    DepDoneMsg *dm = g_new0(DepDoneMsg, 1);
    dm->s = s; dm->any_missing = any;
    g_idle_add(dep_done_cb, dm);

    g_task_return_boolean(task, TRUE);
}

static void start_dep_check(Inst *s)
{
    s->dep_done = FALSE;
    gtk_label_set_text(s->dep_sum, "");

    /* Reset each row: hide icon, show spinner */
    for (int i = 0; i < N_DEPS; i++) {
        gtk_widget_set_visible(GTK_WIDGET(s->dep_icon[i]), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(s->dep_spin[i]), TRUE);
        gtk_spinner_start(s->dep_spin[i]);
        s->dep_st[i] = DEP_PENDING;
    }

    /* Disable Back while check is running to avoid race */
    gtk_widget_set_sensitive(GTK_WIDGET(s->btn_back), FALSE);

    GTask *t = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(t, s, NULL);
    g_task_run_in_thread(t, dep_thread_fn);
    g_object_unref(t);
}

/* ─── Install async ──────────────────────────────────────────────────────── */

typedef struct {
    Inst   *s;
    gchar  *line;
    double  frac;   /* -1 = pulse/no change */
} LogMsg;

typedef struct {
    Inst     *s;
    gboolean  ok;
    gboolean  manual_install; /* show manual-install dialog instead of error */
    gchar    *et;             /* error title */
    gchar    *eb;             /* error body  */
} DoneMsg;

static gboolean log_cb(gpointer data)
{
    LogMsg *m = data;

    if (m->line && m->line[0]) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(m->s->inst_buf, &end);
        gtk_text_buffer_insert(m->s->inst_buf, &end, m->line, -1);
        gtk_text_buffer_insert(m->s->inst_buf, &end, "\n", -1);
        gtk_text_buffer_get_end_iter(m->s->inst_buf, &end);
        GtkTextMark *ins = gtk_text_buffer_get_insert(m->s->inst_buf);
        gtk_text_buffer_move_mark(m->s->inst_buf, ins, &end);
        gtk_text_view_scroll_to_mark(m->s->inst_view, ins, 0.0, TRUE, 0.0, 1.0);
    }

    if (m->frac >= 0)
        gtk_progress_bar_set_fraction(m->s->inst_prog, m->frac);

    g_free(m->line);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static gboolean done_cb(gpointer data)
{
    DoneMsg *m = data;
    if (m->ok) {
        navigate(m->s, 4);
    } else {
        /* Re-expose Back so the user can try again */
        gtk_widget_set_visible(GTK_WIDGET(m->s->btn_back), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(m->s->btn_back), TRUE);
        m->s->cur = 3; /* stay on install page */

#ifndef G_OS_WIN32
        if (m->manual_install)
            show_manual_install(m->s->window);
        else
#endif
            show_error(m->s->window,
                m->et ? m->et : "Something went wrong",
                m->eb ? m->eb
                      : "An unexpected error occurred. "
                        "Please try running the installer again.");
    }
    g_free(m->et);
    g_free(m->eb);
    g_free(m);
    return G_SOURCE_REMOVE;
}

/* Post a log line from the install thread (frac=-1 → no progress change). */
static void emit_log(Inst *s, const char *text, double frac)
{
    LogMsg *m = g_new0(LogMsg, 1);
    m->s    = s;
    m->line = g_strdup(text);
    m->frac = frac;
    g_idle_add(log_cb, m);
}

/*
 * Run *argv in a subprocess, stream every output line to the log.
 * Returns TRUE on success.
 */
static gboolean run_cmd_log(Inst *s, const char **argv, GError **err)
{
    GSubprocess *proc = g_subprocess_newv(
        (const gchar * const *)argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
        err);
    if (!proc) return FALSE;

    GDataInputStream *dis = g_data_input_stream_new(
        g_subprocess_get_stdout_pipe(proc));

    gchar *line;
    while ((line = g_data_input_stream_read_line(dis, NULL, NULL, NULL))) {
        emit_log(s, line, -1);
        g_free(line);
    }
    g_object_unref(dis);

    gboolean ok = g_subprocess_wait_check(proc, NULL, err);
    g_object_unref(proc);
    return ok;
}

/* Signal install failure from the install thread. */
static void fail_install(Inst *s, GTask *task,
                          gboolean manual, const char *title, const char *body)
{
    DoneMsg *m = g_new0(DoneMsg, 1);
    m->s            = s;
    m->ok           = FALSE;
    m->manual_install = manual;
    m->et           = g_strdup(title);
    m->eb           = g_strdup(body);
    g_idle_add(done_cb, m);
    g_task_return_boolean(task, FALSE);
}

#ifdef G_OS_WIN32
static void install_thread_fn(GTask *task, gpointer src_obj, gpointer td,
                               GCancellable *cancel)
{
    (void)src_obj; (void)cancel;
    Inst *s = td;
    GError *err = NULL;

    emit_log(s, "Starting installation\xe2\x80\xa6", 0.02);

    /* ── Step 0: Clean up any previous installation ──────────────────── */
    const char *rel_paths[] = {
        "WhisperDrop\\WhisperDrop.exe",
        "WhisperDrop\\WhisperDrop.png",
        NULL
    };
    for (int i = 0; rel_paths[i]; i++) {
        gchar *p = g_build_filename(g_get_user_data_dir(), rel_paths[i], NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS)) {
            GFile *f = g_file_new_for_path(p);
            g_file_delete(f, NULL, NULL);
            g_object_unref(f);
        }
        g_free(p);
    }
    emit_log(s, "Ready to install.", 0.08);

    /* ── Step 1: Python required — bail if missing ───────────────────── */
    if (s->dep_st[0] == DEP_MISSING) {
        fail_install(s, task, FALSE,
            "Python is required",
            "WhisperDrop needs Python to run the Whisper transcription engine.\n\n"
            "Download and install Python from https://www.python.org/downloads/\n\n"
            "Make sure to check \xe2\x80\x9c""Add Python to PATH\xe2\x80\x9d during installation, "
            "then run this installer again.");
        return;
    }

    /* ── Step 2: FFmpeg required — bail if missing ───────────────────── */
    if (s->dep_st[1] == DEP_MISSING) {
        fail_install(s, task, FALSE,
            "FFmpeg is required",
            "FFmpeg is needed to decode audio and video files.\n\n"
            "Install it with winget (if available):\n"
            "    winget install Gyan.FFmpeg\n\n"
            "Or download it from https://ffmpeg.org/download.html and add it to your PATH,\n"
            "then run this installer again.");
        return;
    }

    /* ── Step 3: Install Whisper via pip (if missing) ────────────────── */
    if (s->dep_st[2] == DEP_MISSING) {
        emit_log(s,
            "Installing Whisper transcription engine "
            "\xe2\x80\x94 this may take a minute\xe2\x80\xa6",
            0.30);

        /* Try "python -m pip" first (most reliable on Windows) */
        const char *pip1[] = { "python", "-m", "pip", "install", "-U",
                                "openai-whisper", NULL };
        if (!run_cmd_log(s, pip1, &err)) {
            g_clear_error(&err);
            emit_log(s, "Trying pip3\xe2\x80\xa6", -1);
            const char *pip2[] = { "pip", "install", "-U", "openai-whisper", NULL };
            if (!run_cmd_log(s, pip2, &err)) {
                gchar *body = g_strdup_printf(
                    "Could not install the Whisper transcription engine.\n\n"
                    "You can install it manually by running:\n"
                    "    python -m pip install -U openai-whisper\n\n"
                    "Details: %s", err ? err->message : "unknown error");
                g_clear_error(&err);
                fail_install(s, task, FALSE, "Failed to install Whisper", body);
                g_free(body);
                return;
            }
        }
        emit_log(s, "Whisper installed successfully.", -1);
    } else {
        emit_log(s, "Transcription engine: already installed.", -1);
    }

    /* ── Step 4: Install binary to %LOCALAPPDATA%\WhisperDrop\ ──────── */
    emit_log(s, "Installing WhisperDrop\xe2\x80\xa6", 0.60);

    gchar *inst_dir = g_build_filename(g_get_user_data_dir(), "WhisperDrop", NULL);
    g_mkdir_with_parents(inst_dir, 0755);
    gchar *dest_bin = g_build_filename(inst_dir, "WhisperDrop.exe", NULL);

    gboolean cp_ok = FALSE;
    if (s->src_bin) {
        GFile *fsrc = g_file_new_for_path(s->src_bin);
        GFile *fdst = g_file_new_for_path(dest_bin);
        cp_ok = g_file_copy(fsrc, fdst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err);
        g_object_unref(fsrc);
        g_object_unref(fdst);
    } else {
        gchar *tmp = extract_embedded_binary();
        if (tmp) {
            GFile *fsrc = g_file_new_for_path(tmp);
            GFile *fdst = g_file_new_for_path(dest_bin);
            cp_ok = g_file_copy(fsrc, fdst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err);
            g_object_unref(fsrc);
            g_object_unref(fdst);
            g_unlink(tmp);
            g_free(tmp);
        } else {
            err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED,
                "Could not extract embedded application data.");
        }
    }

    if (!cp_ok) {
        gchar *body = g_strdup_printf(
            "Could not install the application to:\n%s\n\n"
            "Check that you have write access and try again.\n\nDetails: %s",
            inst_dir, err ? err->message : "unknown error");
        g_clear_error(&err);
        g_free(dest_bin);
        g_free(inst_dir);
        fail_install(s, task, FALSE, "Could not install WhisperDrop", body);
        g_free(body);
        return;
    }
    g_free(dest_bin);
    emit_log(s, "WhisperDrop installed successfully.", 0.75);

    /* ── Step 5: Write icon ───────────────────────────────────────────── */
    gchar *dest_ico = g_build_filename(inst_dir, "WhisperDrop.png", NULL);
    if (s->src_ico) {
        GFile *isrc = g_file_new_for_path(s->src_ico);
        GFile *idst = g_file_new_for_path(dest_ico);
        g_file_copy(isrc, idst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        g_object_unref(isrc);
        g_object_unref(idst);
    } else {
        g_file_set_contents(dest_ico,
            (const gchar *)whisperdrop_icon,
            (gssize)whisperdrop_icon_len, NULL);
    }
    g_free(dest_ico);

    /* ── Step 6: Create Start Menu shortcut via PowerShell ───────────── */
    emit_log(s, "Creating Start Menu shortcut\xe2\x80\xa6", 0.88);

    /* Use environment-variable-based paths to avoid quoting issues */
    const char *ps_cmd[] = {
        "powershell", "-NoProfile", "-Command",
        "$s = New-Object -ComObject WScript.Shell; "
        "$lnk = $s.CreateShortcut("
            "[System.IO.Path]::Combine($env:APPDATA, 'Microsoft', 'Windows', "
            "'Start Menu', 'Programs', 'WhisperDrop.lnk')); "
        "$lnk.TargetPath = [System.IO.Path]::Combine("
            "$env:LOCALAPPDATA, 'WhisperDrop', 'WhisperDrop.exe'); "
        "$lnk.WorkingDirectory = [System.IO.Path]::Combine("
            "$env:LOCALAPPDATA, 'WhisperDrop'); "
        "$lnk.Description = 'WhisperDrop - Audio/Video Transcription'; "
        "$lnk.Save()",
        NULL
    };
    if (!run_cmd_log(s, ps_cmd, &err)) {
        /* Non-fatal — app still works, shortcut just won't be in Start Menu */
        emit_log(s, "Note: could not create Start Menu shortcut (non-fatal).", -1);
        g_clear_error(&err);
    } else {
        emit_log(s, "Start Menu shortcut created.", -1);
    }

    g_free(inst_dir);

    emit_log(s, "Installation complete!", 1.00);

    DoneMsg *dm = g_new0(DoneMsg, 1);
    dm->s  = s;
    dm->ok = TRUE;
    g_idle_add(done_cb, dm);
    g_task_return_boolean(task, TRUE);
}

#else  /* Linux install_thread_fn */

static void install_thread_fn(GTask *task, gpointer src_obj, gpointer td,
                               GCancellable *cancel)
{
    (void)src_obj; (void)cancel;
    Inst *s = td;
    GError *err = NULL;

    emit_log(s, "Starting installation\xe2\x80\xa6", 0.02);

    /* ── Step 0: Clean up any previous installation ───────────────────── */

    /* Desktop entry files — all locations WhisperDrop may have left behind */
    const char *dt_paths[] = {
        ".local/share/applications/WhisperDrop.desktop",
        ".local/share/applications/whisperdrop.desktop",
        ".local/share/applications/com.saguarosec.WhisperDrop.desktop",
        NULL
    };

    gboolean had_previous = FALSE;
    for (int i = 0; dt_paths[i]; i++) {
        gchar *p = g_build_filename(g_get_home_dir(), dt_paths[i], NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS)) {
            had_previous = TRUE;
            GFile *f = g_file_new_for_path(p);
            g_file_delete(f, NULL, NULL);
            g_object_unref(f);
        }
        g_free(p);
    }

    /* Old binary location(s) */
    const char *bin_paths[] = {
        ".local/bin/WhisperDrop",
        ".local/bin/whisperdrop",
        NULL
    };
    for (int i = 0; bin_paths[i]; i++) {
        gchar *p = g_build_filename(g_get_home_dir(), bin_paths[i], NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS)) {
            had_previous = TRUE;
            GFile *f = g_file_new_for_path(p);
            g_file_delete(f, NULL, NULL);
            g_object_unref(f);
        }
        g_free(p);
    }

    /* Old icon(s) */
    const char *ico_paths[] = {
        ".local/share/icons/WhisperDrop.png",
        ".local/share/icons/whisperdrop.png",
        NULL
    };
    for (int i = 0; ico_paths[i]; i++) {
        gchar *p = g_build_filename(g_get_home_dir(), ico_paths[i], NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS)) {
            had_previous = TRUE;
            GFile *f = g_file_new_for_path(p);
            g_file_delete(f, NULL, NULL);
            g_object_unref(f);
        }
        g_free(p);
    }

    if (had_previous) {
        /* Force the desktop database to forget the old entries immediately */
        gchar *apps_dir = g_build_filename(g_get_home_dir(), ".local",
                                           "share", "applications", NULL);
        gchar *upd = g_strdup_printf("update-desktop-database %s", apps_dir);
        g_free(apps_dir);
        g_spawn_command_line_sync(upd, NULL, NULL, NULL, NULL);
        g_free(upd);
        emit_log(s, "Previous installation removed.", 0.05);
    }

    /* ── Step 1: System packages ──────────────────────────────────────── */

    /* Check whether any system package (not Whisper) is missing */
    gboolean need_syspkg = FALSE;
    for (int i = 0; i < N_DEPS; i++) {
        if (i == 1) continue; /* Whisper handled separately */
        if (s->dep_st[i] == DEP_MISSING) { need_syspkg = TRUE; break; }
    }

    if (need_syspkg && s->pkgmgr == PKG_UNKNOWN) {
        /* Cannot auto-install; show the manual-install dialog */
        emit_log(s,
            "Your Linux distribution was not recognized \xe2\x80\x94 "
            "please follow the manual setup instructions.", -1);
        fail_install(s, task, TRUE,
            "Manual setup required",
            "Your Linux distribution was not recognized. "
            "Please see the Setup Guide for instructions.");
        return;
    }

    for (int i = 0; i < N_DEPS; i++) {
        if (i == 1 || s->dep_st[i] == DEP_OK) continue;

        const char *pkg = PKG_NAMES[i][s->pkgmgr - 1];
        if (!pkg) continue;

        gchar *msg = g_strdup_printf("Installing %s\xe2\x80\xa6", DEPS[i].label);
        emit_log(s, msg, 0.10 + (double)i * 0.05);
        g_free(msg);

        /* Build pkexec install command */
        const char *cmd[8];
        int ci = 0;
        cmd[ci++] = "pkexec";
        switch (s->pkgmgr) {
            case PKG_APT:
                cmd[ci++] = "apt-get"; cmd[ci++] = "install"; cmd[ci++] = "-y";
                break;
            case PKG_DNF:
                cmd[ci++] = "dnf"; cmd[ci++] = "install"; cmd[ci++] = "-y";
                break;
            case PKG_PACMAN:
                cmd[ci++] = "pacman"; cmd[ci++] = "-S"; cmd[ci++] = "--noconfirm";
                break;
            case PKG_ZYPPER:
                cmd[ci++] = "zypper"; cmd[ci++] = "install"; cmd[ci++] = "-y";
                break;
            default: break;
        }
        cmd[ci++] = pkg;
        cmd[ci]   = NULL;

        if (!run_cmd_log(s, cmd, &err)) {
            gchar *body = g_strdup_printf(
                "Could not install %s automatically.\n\n"
                "You may need to install it yourself using your system\xe2\x80\x99s "
                "software manager and then run the installer again.\n\n"
                "Details: %s",
                DEPS[i].label, err ? err->message : "unknown error");
            gchar *title = g_strdup_printf("Failed to install %s", DEPS[i].label);
            g_clear_error(&err);
            fail_install(s, task, FALSE, title, body);
            g_free(title);
            g_free(body);
            return;
        }

        gchar *done = g_strdup_printf("%s installed successfully.", DEPS[i].label);
        emit_log(s, done, -1);
        g_free(done);
    }

    /* ── Step 2: Whisper (pip install, no pkexec) ────────────────────── */

    if (s->dep_st[1] == DEP_MISSING) {
        emit_log(s,
            "Installing Whisper transcription engine "
            "\xe2\x80\x94 this may take a minute\xe2\x80\xa6",
            0.40);

        const char *pip1[] = { "pip", "install", "-U", "openai-whisper", NULL };
        if (!run_cmd_log(s, pip1, &err)) {
            g_clear_error(&err);
            emit_log(s, "Trying alternate installation method\xe2\x80\xa6", -1);
            const char *pip2[] = {
                "pip", "install", "--break-system-packages",
                "-U", "openai-whisper", NULL
            };
            if (!run_cmd_log(s, pip2, &err)) {
                gchar *body = g_strdup_printf(
                    "Could not install the Whisper transcription engine.\n\n"
                    "You can install it yourself by running:\n"
                    "    pip install -U openai-whisper\n\n"
                    "Details: %s",
                    err ? err->message : "unknown error");
                g_clear_error(&err);
                fail_install(s, task, FALSE, "Failed to install Whisper", body);
                g_free(body);
                return;
            }
        }
        emit_log(s, "Whisper installed successfully.", -1);
    } else {
        emit_log(s, "Transcription engine: already installed.", -1);
    }

    /* ── Step 3: Install binary ──────────────────────────────────────── */

    emit_log(s, "Installing WhisperDrop\xe2\x80\xa6", 0.55);

    gchar *bin_dir  = g_build_filename(g_get_home_dir(), ".local", "bin", NULL);
    g_mkdir_with_parents(bin_dir, 0755);
    gchar *dest_bin = g_build_filename(bin_dir, "WhisperDrop", NULL);
    g_free(bin_dir);

    gboolean cp_ok = FALSE;

    if (s->src_bin) {
        /* Adjacent binary found — copy it directly */
        GFile *fsrc = g_file_new_for_path(s->src_bin);
        GFile *fdst = g_file_new_for_path(dest_bin);
        cp_ok = g_file_copy(fsrc, fdst,
            G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err);
        g_object_unref(fsrc);
        g_object_unref(fdst);
    } else {
        /* No adjacent binary — extract the embedded copy */
        gchar *tmp = extract_embedded_binary();
        if (tmp) {
            GFile *fsrc = g_file_new_for_path(tmp);
            GFile *fdst = g_file_new_for_path(dest_bin);
            cp_ok = g_file_copy(fsrc, fdst,
                G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err);
            g_object_unref(fsrc);
            g_object_unref(fdst);
            g_unlink(tmp);
            g_free(tmp);
        } else {
            /* Should never happen — embedded data is always present */
            err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED,
                "Could not extract embedded application data.");
        }
    }

    if (!cp_ok) {
        gchar *body = g_strdup_printf(
            "Could not install the application.\n\n"
            "Please check that you have write access to your home folder "
            "and try again.\n\nDetails: %s",
            err ? err->message : "unknown error");
        g_clear_error(&err);
        g_free(dest_bin);
        fail_install(s, task, FALSE, "Could not install WhisperDrop", body);
        g_free(body);
        return;
    }

    g_chmod(dest_bin, 0755);
    g_free(dest_bin);
    emit_log(s, "WhisperDrop installed successfully.", 0.70);

    /* ── Step 4: Application icon ────────────────────────────────────── */

    gchar *icons_dir = g_build_filename(g_get_home_dir(), ".local",
                                        "share", "icons", NULL);
    g_mkdir_with_parents(icons_dir, 0755);
    gchar *dest_ico = g_build_filename(icons_dir, "WhisperDrop.png", NULL);
    g_free(icons_dir);

    if (s->src_ico) {
        /* Copy from a file found on disk */
        GFile *isrc = g_file_new_for_path(s->src_ico);
        GFile *idst = g_file_new_for_path(dest_ico);
        g_file_copy(isrc, idst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        g_object_unref(isrc);
        g_object_unref(idst);
    } else {
        /* Write the embedded PNG bytes directly */
        g_file_set_contents(dest_ico,
            (const gchar *)whisperdrop_icon,
            (gssize)whisperdrop_icon_len, NULL);
    }
    g_free(dest_ico);

    /* ── Step 5: .desktop file ───────────────────────────────────────── */

    emit_log(s, "Creating application shortcut\xe2\x80\xa6", 0.82);

    gchar *apps_dir  = g_build_filename(g_get_home_dir(), ".local",
                                        "share", "applications", NULL);
    g_mkdir_with_parents(apps_dir, 0755);

    gchar *local_bin = g_build_filename(g_get_home_dir(), ".local", "bin", NULL);
    gchar *local_ico = g_build_filename(g_get_home_dir(), ".local",
                                        "share", "icons", NULL);
    gchar *desktop = g_strdup_printf(
        "[Desktop Entry]\n"
        "Name=WhisperDrop\n"
        "Comment=Drag and drop audio or video files to get text transcripts\n"
        "Exec=%s/WhisperDrop\n"
        "Icon=%s/WhisperDrop\n"
        "Type=Application\n"
        "Categories=AudioVideo;Utility;\n"
        "Terminal=false\n",
        local_bin, local_ico);
    g_free(local_bin);
    g_free(local_ico);

    gchar *dt_path = g_build_filename(apps_dir, "WhisperDrop.desktop", NULL);
    g_free(apps_dir);

    if (!g_file_set_contents(dt_path, desktop, -1, &err)) {
        gchar *body = g_strdup_printf(
            "Could not write the application shortcut file.\n\nDetails: %s",
            err ? err->message : "unknown error");
        g_clear_error(&err);
        g_free(desktop);
        g_free(dt_path);
        fail_install(s, task, FALSE,
            "Could not create application shortcut", body);
        g_free(body);
        return;
    }
    g_free(desktop);
    g_free(dt_path);

    /* Refresh the application database so the shortcut appears immediately */
    gchar *db_dir = g_build_filename(g_get_home_dir(), ".local",
                                     "share", "applications", NULL);
    gchar *upd    = g_strdup_printf("update-desktop-database %s", db_dir);
    g_free(db_dir);
    g_spawn_command_line_async(upd, NULL);
    g_free(upd);

    emit_log(s, "Application shortcut created.", 0.95);
    emit_log(s, "Installation complete!", 1.00);

    DoneMsg *dm = g_new0(DoneMsg, 1);
    dm->s  = s;
    dm->ok = TRUE;
    g_idle_add(done_cb, dm);
    g_task_return_boolean(task, TRUE);
}

#endif /* G_OS_WIN32 / Linux install_thread_fn */

static void start_install(Inst *s)
{
#ifndef G_OS_WIN32
    s->pkgmgr = detect_pkg();
#endif

    GTask *t = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(t, s, NULL);
    g_task_run_in_thread(t, install_thread_fn);
    g_object_unref(t);
}

/* ─── Page builders ──────────────────────────────────────────────────────── */

static GtkWidget *build_welcome(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(box, TRUE);

    GtkWidget *lbl = gtk_label_new(
#ifdef G_OS_WIN32
        "WhisperDrop turns your audio and video files into text \xe2\x80\x94 "
        "just drag and drop them onto the window. "
        "Everything runs on your computer; nothing is sent to the cloud.\n\n"
        "This installer will:\n"
        "  \xe2\x80\xa2  Check that Python and FFmpeg are installed\n"
        "  \xe2\x80\xa2  Install the Whisper transcription engine automatically\n"
        "  \xe2\x80\xa2  Copy WhisperDrop to your user profile\n"
        "  \xe2\x80\xa2  Add WhisperDrop to your Start Menu\n\n"
        "Python and FFmpeg must be installed before running this installer.\n"
        "No administrator password is required."
#else
        "WhisperDrop turns your audio and video files into text \xe2\x80\x94 "
        "just drag and drop them onto the window. "
        "Everything runs on your computer; nothing is sent to the cloud.\n\n"
        "This installer will:\n"
        "  \xe2\x80\xa2  Check that your system has everything WhisperDrop needs\n"
        "  \xe2\x80\xa2  Install any missing pieces automatically\n"
        "  \xe2\x80\xa2  Add WhisperDrop to your Applications menu\n\n"
        "You may be asked for your password once to install system software."
#endif
    );
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(box), lbl);

    return box;
}

static GtkWidget *build_license(Inst *s)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    /* Scrollable license text */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), MIT_LICENSE, -1);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_box_append(GTK_BOX(box), scroll);

    /* Accept checkbox */
    GtkWidget *cb = gtk_check_button_new_with_label(
        "I accept the license terms");
    s->lic_cb = GTK_CHECK_BUTTON(cb);
    gtk_widget_set_margin_top(cb, 4);
    gtk_box_append(GTK_BOX(box), cb);

    /* Re-evaluate Next button whenever the checkbox changes */
    g_signal_connect_swapped(cb, "toggled", G_CALLBACK(update_buttons), s);

    return box;
}

static GtkWidget *build_depcheck(Inst *s)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(box, TRUE);

    GtkWidget *intro = gtk_label_new(
        "Checking that your system has everything WhisperDrop needs\xe2\x80\xa6");
    gtk_label_set_xalign(GTK_LABEL(intro), 0.0f);
    gtk_widget_set_margin_bottom(intro, 16);
    gtk_box_append(GTK_BOX(box), intro);

    for (int i = 0; i < N_DEPS; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_bottom(row, 6);

        /* Spinner — visible while check is in progress */
        GtkWidget *spin = gtk_spinner_new();
        gtk_widget_set_size_request(spin, 20, 20);
        gtk_spinner_start(GTK_SPINNER(spin));
        s->dep_spin[i] = GTK_SPINNER(spin);
        gtk_box_append(GTK_BOX(row), spin);

        /* Status icon — hidden until check completes */
        GtkWidget *icon = gtk_label_new(NULL);
        gtk_widget_set_size_request(icon, 20, -1);
        gtk_widget_set_visible(icon, FALSE);
        s->dep_icon[i] = GTK_LABEL(icon);
        gtk_box_append(GTK_BOX(row), icon);

        /* Plain-English description */
        GtkWidget *desc = gtk_label_new(DEPS[i].label);
        gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
        gtk_widget_set_hexpand(desc, TRUE);
        gtk_box_append(GTK_BOX(row), desc);

        gtk_box_append(GTK_BOX(box), row);
    }

    /* Summary line — updated when check finishes */
    GtkWidget *sum = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(sum), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(sum), TRUE);
    gtk_widget_set_margin_top(sum, 16);
    s->dep_sum = GTK_LABEL(sum);
    gtk_box_append(GTK_BOX(box), sum);

    return box;
}

static GtkWidget *build_install(Inst *s)
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
    s->inst_prog = GTK_PROGRESS_BAR(prog);
    gtk_box_append(GTK_BOX(box), prog);

    /* Scrollable log */
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
    s->inst_buf  = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    s->inst_view = GTK_TEXT_VIEW(view);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_box_append(GTK_BOX(box), scroll);

    return box;
}

static GtkWidget *build_finish(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 32);
    gtk_widget_set_margin_end(box, 32);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(box, TRUE);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading),
        "<span size=\"x-large\" weight=\"bold\">WhisperDrop is ready!</span>");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *body = gtk_label_new(
#ifdef G_OS_WIN32
        "You can now find WhisperDrop in your Start Menu.\n\n"
        "Drag and drop any audio or video file onto it to get a text "
        "transcript \xe2\x80\x94 no cloud, no subscription, no command line."
#else
        "You can now find WhisperDrop in your Applications menu.\n\n"
        "Drag and drop any audio or video file onto it to get a text "
        "transcript \xe2\x80\x94 no cloud, no subscription, no command line."
#endif
    );
    gtk_label_set_wrap(GTK_LABEL(body), TRUE);
    gtk_label_set_xalign(GTK_LABEL(body), 0.0f);
    gtk_box_append(GTK_BOX(box), body);

    return box;
}

/* ─── Window construction ────────────────────────────────────────────────── */

static void build_window(Inst *s)
{
    /* Main window */
    GtkWidget *win = gtk_application_window_new(s->gapp);
    s->window = GTK_WINDOW(win);
    gtk_window_set_title(GTK_WINDOW(win), "WhisperDrop Installer");
    gtk_window_set_default_size(GTK_WINDOW(win), WIN_W, WIN_H);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

    /* Outer box — everything stacks vertically */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), outer);

    /* ── Header (logo + page title) ─────────────────────────────────── */
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
        logo = gtk_image_new_from_icon_name("application-x-executable");
        gtk_image_set_pixel_size(GTK_IMAGE(logo), 64);
    }
    gtk_widget_set_halign(logo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(hdr), logo);

    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_lbl), "<b>Welcome to WhisperDrop</b>");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_CENTER);
    s->hdr_title = GTK_LABEL(title_lbl);
    gtk_box_append(GTK_BOX(hdr), title_lbl);

    gtk_box_append(GTK_BOX(outer), hdr);
    gtk_box_append(GTK_BOX(outer),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Page stack ─────────────────────────────────────────────────── */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),
        GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 200);
    gtk_widget_set_vexpand(stack, TRUE);
    s->stack = GTK_STACK(stack);

    gtk_stack_add_named(GTK_STACK(stack), build_welcome(),   PG_NAME[0]);
    gtk_stack_add_named(GTK_STACK(stack), build_license(s),  PG_NAME[1]);
    gtk_stack_add_named(GTK_STACK(stack), build_depcheck(s), PG_NAME[2]);
    gtk_stack_add_named(GTK_STACK(stack), build_install(s),  PG_NAME[3]);
    gtk_stack_add_named(GTK_STACK(stack), build_finish(),    PG_NAME[4]);

    gtk_box_append(GTK_BOX(outer), stack);
    gtk_box_append(GTK_BOX(outer),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Button bar ─────────────────────────────────────────────────── */
    GtkWidget *btnbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(btnbar, 12);
    gtk_widget_set_margin_end(btnbar, 12);
    gtk_widget_set_margin_top(btnbar, 8);
    gtk_widget_set_margin_bottom(btnbar, 8);

    GtkWidget *back = gtk_button_new_with_label("Back");
    s->btn_back = GTK_BUTTON(back);
    g_signal_connect(back, "clicked", G_CALLBACK(on_back), s);
    gtk_box_append(GTK_BOX(btnbar), back);

    /* Spacer */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(btnbar), spacer);

    /* Launch (finish page only) */
    GtkWidget *launch = gtk_button_new_with_label("Launch WhisperDrop");
    s->btn_launch = GTK_BUTTON(launch);
    gtk_widget_set_visible(launch, FALSE);
    gtk_widget_add_css_class(launch, "suggested-action");
    g_signal_connect(launch, "clicked", G_CALLBACK(on_launch), s);
    gtk_box_append(GTK_BOX(btnbar), launch);

    /* Next / Install / Close */
    GtkWidget *next = gtk_button_new_with_label("Next");
    s->btn_next = GTK_BUTTON(next);
    gtk_widget_add_css_class(next, "suggested-action");
    g_signal_connect(next, "clicked", G_CALLBACK(on_next), s);
    gtk_box_append(GTK_BOX(btnbar), next);

    gtk_box_append(GTK_BOX(outer), btnbar);

    /* Show first page */
    navigate(s, 0);
    gtk_window_present(GTK_WINDOW(win));
}

/* ─── Application callbacks ──────────────────────────────────────────────── */

static void on_activate(GtkApplication *gapp, gpointer ud)
{
    Inst *s = ud;
    s->gapp = gapp;
    build_window(s);
}

int main(int argc, char **argv)
{
    gchar *exe_dir = get_exe_dir();

    Inst *s    = g_new0(Inst, 1);
    s->src_bin = find_binary(exe_dir);
    s->src_ico = find_icon(exe_dir);
    g_free(exe_dir);

    GtkApplication *app = gtk_application_new(
        "com.saguarosec.WhisperDropInstaller",
        G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), s);

    int ret = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    g_free(s->src_bin);
    g_free(s->src_ico);
    g_free(s);
    return ret;
}

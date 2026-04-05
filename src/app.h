#pragma once
#include <gtk/gtk.h>

/* ---- App metadata ---- */
#define APP_NAME      "WhisperDrop"
#define APP_VERSION   "0.5.0"
#define APP_AUTHOR    "Michael Galde"
#define APP_EMAIL     "WhisperDrop@saguarosec.com"
#define APP_WEBSITE   "https://saguarosec.com/"
#define APP_GITHUB    "https://github.com/mgalde"
#define GITHUB_REPO   "mgalde/WhisperDrop"
#define ICON_RESOURCE "/com/saguarosec/whisperdrop/WhisperDrop.png"

/* ---- Job ---- */

typedef enum {
    JOB_QUEUED    = 0,
    JOB_RUNNING   = 1,
    JOB_DONE      = 2,
    JOB_ERROR     = 3,
    JOB_CANCELLED = 4,
} JobStatus;

typedef struct {
    gchar     *path;
    JobStatus  status;
    gchar     *out_dir;
    gchar     *error_msg;
    GtkWidget *row;    /* GtkListBoxRow — owned by the list box */
    GtkWidget *label;  /* GtkLabel inside the row */
} Job;

/* ---- AppState ---- */

typedef struct _AppState AppState;
struct _AppState {
    GtkApplicationWindow *window;

    /* Top controls */
    GtkWidget *model_combo;        /* GtkComboBoxText (editable) */
    GtkWidget *format_combo;       /* GtkComboBoxText */
    GtkWidget *same_folder_check;  /* GtkCheckButton */
    GtkWidget *out_folder_entry;   /* GtkEntry */
    GtkWidget *out_folder_btn;     /* GtkButton */
    GtkWidget *extra_args_entry;   /* GtkEntry */

    /* File area */
    GtkWidget  *drop_frame;
    GtkListBox *file_list;

    /* List action buttons */
    GtkWidget *add_btn;
    GtkWidget *remove_btn;
    GtkWidget *clear_btn;

    /* Run controls */
    GtkWidget      *start_btn;
    GtkWidget      *stop_btn;
    GtkWidget      *open_out_btn;
    GtkProgressBar *progress;

    /* Log */
    GtkTextBuffer *log_buf;
    GtkWidget     *log_view;

    /* Status label (replaces GtkStatusbar) */
    GtkWidget *status_label;

    /* Job queue */
    GPtrArray *jobs;  /* GPtrArray<Job *> */

    /* Worker thread */
    GThread  *worker_thread;
    GMutex    worker_mutex;
    gboolean  cancel_requested;
    GPid      current_pid;

    /* Status heartbeat */
    guint    status_timer_id;
    gint64   run_started_at;   /* g_get_monotonic_time() at start of run */
    gboolean stop_requested;

    /* Update check */
    gboolean update_checked;

    /* Persistent settings */
    GKeyFile *keyfile;
    gchar    *settings_path;
};

/* ---- Job helpers ---- */
Job        *job_new         (const gchar *path);
void        job_free        (Job *job);
const gchar *job_status_str (JobStatus s);
gchar       *job_label_text (const Job *job);  /* caller must g_free */

/* ---- AppState lifecycle ---- */
AppState *app_state_new  (void);
void      app_state_free (AppState *app);

/* ---- Settings ---- */
void app_load_settings (AppState *app);
void app_save_settings (AppState *app);

/* ---- UI helpers (main thread only) ---- */
void  app_add_path    (AppState *app, const gchar *path);
void  app_append_log  (AppState *app, const gchar *text);
void  app_set_status  (AppState *app, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);
void  app_set_running (AppState *app, gboolean running);
void  app_refresh_row (AppState *app, guint idx);

/* ---- GtkApplication activate callback ---- */
void app_activate (GtkApplication *gapp, gpointer user_data);

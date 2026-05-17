#include "worker.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

/* ==========================================================
   Message types posted to the main thread via g_idle_add
   ========================================================== */

typedef struct {
    AppState *app;
    gchar    *text;
} LogMsg;

typedef struct {
    AppState  *app;
    guint      idx;
    JobStatus  new_status;
    gchar     *out_dir;    /* NULL if not updating */
    gchar     *error_msg;  /* NULL if none */
} JobUpdateMsg;

typedef struct {
    AppState *app;
    gdouble   fraction;
} ProgressMsg;

typedef struct {
    AppState *app;
    gchar    *error;   /* NULL = normal finish, non-NULL = fatal */
} DoneMsg;

/* ==========================================================
   Idle callbacks — run on main thread
   ========================================================== */

static gboolean idle_log(gpointer data) {
    LogMsg *m = data;
    app_append_log(m->app, m->text);
    g_free(m->text);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static gboolean idle_job_update(gpointer data) {
    JobUpdateMsg *m = data;
    if (m->idx < m->app->jobs->len) {
        Job *job      = m->app->jobs->pdata[m->idx];
        job->status   = m->new_status;
        if (m->out_dir) {
            g_free(job->out_dir);
            job->out_dir = g_strdup(m->out_dir);
        }
        if (m->error_msg) {
            g_free(job->error_msg);
            job->error_msg = g_strdup(m->error_msg);
        }
        app_refresh_row(m->app, m->idx);
    }
    g_free(m->out_dir);
    g_free(m->error_msg);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static gboolean idle_progress(gpointer data) {
    ProgressMsg *m = data;
    gtk_progress_bar_set_fraction(m->app->progress, m->fraction);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static gboolean idle_done(gpointer data) {
    DoneMsg  *m   = data;
    AppState *app = m->app;

    /* Stop heartbeat */
    if (app->status_timer_id) {
        g_source_remove(app->status_timer_id);
        app->status_timer_id = 0;
    }
    app->run_started_at = 0;
    app->stop_requested = FALSE;

    if (m->error) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", m->error);
        gtk_window_present(GTK_WINDOW(dlg));
        g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
        app_set_status(app, "Error.");
    } else {
        app_append_log(app, "\nAll jobs finished.\n");
        gtk_progress_bar_set_fraction(app->progress, 1.0);
        app_set_status(app, "Done.");
    }

    /* Join the thread (it has returned by the time this idle fires) */
    if (app->worker_thread) {
        g_thread_join(app->worker_thread);
        app->worker_thread = NULL;
    }

    app_set_running(app, FALSE);
    g_free(m->error);
    g_free(m);
    return G_SOURCE_REMOVE;
}

/* ==========================================================
   Helpers to post messages from the worker thread
   ========================================================== */

static void send_log(AppState *app, const gchar *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogMsg *m = g_new(LogMsg, 1);
    m->app  = app;
    m->text = g_strdup_vprintf(fmt, args);
    va_end(args);
    g_idle_add(idle_log, m);
}

static void send_job_update(AppState *app, guint idx, JobStatus status,
                             const gchar *out_dir, const gchar *error_msg) {
    JobUpdateMsg *m = g_new0(JobUpdateMsg, 1);
    m->app        = app;
    m->idx        = idx;
    m->new_status = status;
    m->out_dir    = g_strdup(out_dir);
    m->error_msg  = g_strdup(error_msg);
    g_idle_add(idle_job_update, m);
}

static void send_progress(AppState *app, gdouble fraction) {
    ProgressMsg *m = g_new(ProgressMsg, 1);
    m->app      = app;
    m->fraction = fraction;
    g_idle_add(idle_progress, m);
}

static void send_done(AppState *app, const gchar *error) {
    DoneMsg *m = g_new0(DoneMsg, 1);
    m->app   = app;
    m->error = g_strdup(error);
    g_idle_add(idle_done, m);
}

/* ==========================================================
   Worker thread
   ========================================================== */

typedef struct {
    AppState *app;
    gchar    *model;
    gchar    *out_format;
    gboolean  same_folder;
    gchar    *out_folder;
    gchar    *extra_args;
} WorkerData;

static gpointer worker_thread_func(gpointer data) {
    WorkerData *wd  = data;
    AppState   *app = wd->app;

    gchar *whisper_exe = g_find_program_in_path("whisper");
    if (!whisper_exe) {
        send_done(app,
            "Could not find the `whisper` command.\n\n"
            "Install openai-whisper so the `whisper` executable is on your PATH, "
            "then restart this app.");
        goto cleanup;
    }

    guint total = app->jobs->len;
    if (total == 0) {
        send_done(app, NULL);
        goto cleanup;
    }

    for (guint idx = 0; idx < total; idx++) {

        /* Check for cancellation before starting this job */
        g_mutex_lock(&app->worker_mutex);
        gboolean cancelled = app->cancel_requested;
        g_mutex_unlock(&app->worker_mutex);

        if (cancelled) {
            for (guint i = idx; i < total; i++)
                send_job_update(app, i, JOB_CANCELLED, NULL, NULL);
            break;
        }

        /* job->path is set at creation and never modified — safe to read */
        const gchar *input_path = ((Job *)app->jobs->pdata[idx])->path;

        /* Determine output directory */
        gchar *in_dir  = g_path_get_dirname(input_path);
        gchar *out_dir = (wd->same_folder || !wd->out_folder || wd->out_folder[0] == '\0')
                         ? g_strdup(in_dir) : g_strdup(wd->out_folder);
        g_free(in_dir);

        send_job_update(app, idx, JOB_RUNNING, NULL, NULL);

        /* Build argv */
        GPtrArray *argv = g_ptr_array_new();
        g_ptr_array_add(argv, whisper_exe);
        g_ptr_array_add(argv, (gpointer)input_path);
        g_ptr_array_add(argv, "--model");
        g_ptr_array_add(argv, wd->model);
        g_ptr_array_add(argv, "--output_format");
        g_ptr_array_add(argv, wd->out_format);
        g_ptr_array_add(argv, "--output_dir");
        g_ptr_array_add(argv, out_dir);

        /* Parse and append extra args */
        gchar **extra_argv = NULL;
        gint    extra_argc  = 0;
        if (wd->extra_args && wd->extra_args[0]) {
            GError *parse_err = NULL;
            if (g_shell_parse_argv(wd->extra_args, &extra_argc, &extra_argv, &parse_err)) {
                for (gint i = 0; i < extra_argc; i++)
                    g_ptr_array_add(argv, extra_argv[i]);
            } else {
                send_log(app, "\u26A0 Could not parse extra args: %s\n", parse_err->message);
                g_error_free(parse_err);
            }
        }
        g_ptr_array_add(argv, NULL);

        /* Log the command */
        GString *cmd_str = g_string_new("\n\u25B6 Running:");
        for (guint i = 0; argv->pdata[i] != NULL; i++) {
            gchar *q = g_shell_quote(argv->pdata[i]);
            g_string_append_c(cmd_str, ' ');
            g_string_append(cmd_str, q);
            g_free(q);
        }
        g_string_append_c(cmd_str, '\n');
        send_log(app, "%s", cmd_str->str);
        g_string_free(cmd_str, TRUE);

        send_progress(app, (gdouble)idx / (gdouble)total);

        gboolean was_cancelled = FALSE;
        gboolean job_ok        = FALSE;
        int      exit_code     = 1;

#ifdef G_OS_WIN32
        /* ── Windows: GSubprocessLauncher merges stderr into stdout ──────── */
        GError             *spawn_err = NULL;
        GSubprocessLauncher *launcher = g_subprocess_launcher_new(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
        /* PYTHONUNBUFFERED=1 — flush each line immediately, not block-buffered */
        g_subprocess_launcher_setenv(launcher, "PYTHONUNBUFFERED", "1", TRUE);
        GSubprocess *subproc = g_subprocess_launcher_spawnv(
            launcher, (const gchar * const *)argv->pdata, &spawn_err);
        g_object_unref(launcher);

        g_ptr_array_free(argv, TRUE);
        if (extra_argv) g_strfreev(extra_argv);

        if (!subproc) {
            send_log(app, "\u2716 Failed to start whisper: %s\n", spawn_err->message);
            send_job_update(app, idx, JOB_ERROR, NULL, spawn_err->message);
            g_error_free(spawn_err);
            g_free(out_dir);
            continue;
        }

        /* Store subprocess so worker_cancel can terminate it */
        g_mutex_lock(&app->worker_mutex);
        app->current_proc = subproc;
        g_mutex_unlock(&app->worker_mutex);

        /* Stream combined stdout+stderr line-by-line */
        GDataInputStream *dis = g_data_input_stream_new(
            g_subprocess_get_stdout_pipe(subproc));
        gchar *line;
        while ((line = g_data_input_stream_read_line(dis, NULL, NULL, NULL)) != NULL) {
            send_log(app, "%s\n", line);
            g_free(line);
        }
        g_object_unref(dis);

        g_subprocess_wait(subproc, NULL, NULL);
        job_ok    = g_subprocess_get_successful(subproc);
        exit_code = g_subprocess_get_exit_status(subproc);

        g_mutex_lock(&app->worker_mutex);
        app->current_proc = NULL;
        was_cancelled     = app->cancel_requested;
        g_mutex_unlock(&app->worker_mutex);

        g_object_unref(subproc);

#else  /* POSIX (Linux / macOS) */
        /* ── POSIX: pipe + g_spawn_async_with_fds ────────────────────────── */
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            send_log(app, "\u2716 Failed to create pipe.\n");
            send_job_update(app, idx, JOB_ERROR, NULL, "pipe() failed");
            g_ptr_array_free(argv, TRUE);
            if (extra_argv) g_strfreev(extra_argv);
            g_free(out_dir);
            continue;
        }

        /* PYTHONUNBUFFERED=1 — flush each line immediately, not block-buffered */
        gchar  **envp      = g_get_environ();
        envp               = g_environ_setenv(envp, "PYTHONUNBUFFERED", "1", TRUE);
        GError  *spawn_err = NULL;
        GPid     pid;
        gboolean spawned   = g_spawn_async_with_fds(
            NULL, (gchar **)argv->pdata, envp,
            G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid,
            -1, pipefd[1], pipefd[1], &spawn_err);
        g_strfreev(envp);

        close(pipefd[1]);  /* parent closes the write end */
        g_ptr_array_free(argv, TRUE);
        if (extra_argv) g_strfreev(extra_argv);

        if (!spawned) {
            close(pipefd[0]);
            send_log(app, "\u2716 Failed to start whisper: %s\n", spawn_err->message);
            send_job_update(app, idx, JOB_ERROR, NULL, spawn_err->message);
            g_error_free(spawn_err);
            g_free(out_dir);
            continue;
        }

        /* Store PID so worker_cancel can SIGTERM it */
        g_mutex_lock(&app->worker_mutex);
        app->current_pid = pid;
        g_mutex_unlock(&app->worker_mutex);

        /* Stream output line-by-line; pipe closes naturally when child exits */
        FILE *stream = fdopen(pipefd[0], "r");
        if (stream) {
            char linebuf[4096];
            while (fgets(linebuf, sizeof(linebuf), stream) != NULL) {
                size_t slen = strlen(linebuf);
                if (slen > 0 && linebuf[slen - 1] == '\n') linebuf[slen - 1] = '\0';
                send_log(app, "%s\n", linebuf);
            }
            fclose(stream);
        } else {
            close(pipefd[0]);
        }

        int wstatus = 0;
        waitpid((pid_t)pid, &wstatus, 0);
        g_spawn_close_pid(pid);

        g_mutex_lock(&app->worker_mutex);
        app->current_pid = 0;
        was_cancelled    = app->cancel_requested;
        g_mutex_unlock(&app->worker_mutex);

        job_ok    = WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;
        exit_code = WEXITSTATUS(wstatus);
#endif /* G_OS_WIN32 */

        if (was_cancelled) {
            send_job_update(app, idx, JOB_CANCELLED, NULL, NULL);
            send_log(app, "\u23F9 Cancelled.\n");
        } else if (job_ok) {
            send_job_update(app, idx, JOB_DONE, out_dir, NULL);
            send_log(app, "\u2713 Done.\n");
        } else {
            gchar *errmsg = g_strdup_printf("whisper exited with code %d", exit_code);
            send_log(app, "\u2716 Error: %s\n", errmsg);
            send_job_update(app, idx, JOB_ERROR, NULL, errmsg);
            g_free(errmsg);
        }

        send_progress(app, (gdouble)(idx + 1) / (gdouble)total);
        g_free(out_dir);
    }

    send_done(app, NULL);

cleanup:
    g_free(whisper_exe);
    g_free(wd->model);
    g_free(wd->out_format);
    g_free(wd->out_folder);
    g_free(wd->extra_args);
    g_free(wd);
    return NULL;
}

/* ==========================================================
   Public API
   ========================================================== */

void worker_start(AppState *app) {
    WorkerData *wd = g_new0(WorkerData, 1);
    wd->app = app;

    wd->model       = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->model_combo));
    wd->out_format  = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->format_combo));
    wd->same_folder = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->same_folder_check));
    wd->out_folder  = g_strdup(gtk_editable_get_text(GTK_EDITABLE(app->out_folder_entry)));
    wd->extra_args  = g_strdup(gtk_editable_get_text(GTK_EDITABLE(app->extra_args_entry)));

    g_mutex_lock(&app->worker_mutex);
    app->cancel_requested = FALSE;
    app->current_pid      = 0;
    g_mutex_unlock(&app->worker_mutex);

    app->worker_thread = g_thread_new("whisper-worker", worker_thread_func, wd);
}

void worker_cancel(AppState *app) {
    g_mutex_lock(&app->worker_mutex);
    app->cancel_requested = TRUE;
#ifdef G_OS_WIN32
    if (app->current_proc)
        g_subprocess_force_exit(app->current_proc);  /* requires GLib >= 2.74 */
#else
    if (app->current_pid > 0)
        kill((pid_t)app->current_pid, SIGTERM);
#endif
    g_mutex_unlock(&app->worker_mutex);
}

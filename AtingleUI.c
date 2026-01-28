#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <glib.h>

// --- Global Widgets ---
static GtkWidget *main_window;
static GtkWidget *editor;

// --- Helper: Get Directory of Current Executable ---
// This ensures we find 'injector' even if you run the app from a shortcut
char* get_dir_of_executable() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        char *last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0'; // Truncate filename, keep dir
            return g_strdup(path);
        }
    }
    return g_strdup("."); // Fallback
}

// --- Helper: Find Process ID ---
pid_t findPID(const char *target_name) {
    DIR* d = opendir("/proc");
    if (!d) return -1;

    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_type != DT_DIR) continue;
        pid_t pid = atoi(e->d_name);
        if (pid <= 0) continue;

        char exe_path[PATH_MAX];
        char link_target[PATH_MAX];
        snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", pid);

        ssize_t len = readlink(exe_path, link_target, sizeof(link_target) - 1);
        if (len == -1) continue;
        link_target[len] = '\0';

        if (strstr(link_target, target_name)) {
            closedir(d);
            return pid;
        }
    }
    closedir(d);
    return -1;
}

// --- Button Actions ---

static void on_execute_clicked(GtkButton *button, gpointer user_data) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    
    // Logic to actually run the script would go here
    g_print("Execute Script:\n%s\n", text);
    g_free(text);
}

static void on_clear_clicked(GtkButton *button, gpointer user_data) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    gtk_text_buffer_set_text(buffer, "", -1);
}

// --- File Operations (Async) ---

static void open_file_response(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GFile *file = gtk_file_dialog_open_finish(dialog, res, NULL);
    
    if (file) {
        char *content = NULL;
        gsize length = 0;
        GError *error = NULL;

        if (g_file_load_contents(file, NULL, &content, &length, NULL, &error)) {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
            gtk_text_buffer_set_text(buffer, content, length);
            g_free(content);
        } else {
            g_printerr("Error loading file: %s\n", error->message);
            g_error_free(error);
        }
        g_object_unref(file);
    }
}

static void on_open_file_clicked(GtkButton *button, gpointer user_data) {
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open Script");
    gtk_file_dialog_open(dialog, GTK_WINDOW(main_window), NULL, open_file_response, NULL);
}

static void save_file_response(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GFile *file = gtk_file_dialog_save_finish(dialog, res, NULL);
    
    if (file) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_end_iter(buffer, &end);
        gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        
        GError *error = NULL;
        if (!g_file_replace_contents(file, text, strlen(text), NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &error)) {
             g_printerr("Error saving file: %s\n", error->message);
             g_error_free(error);
        }
        
        g_free(text);
        g_object_unref(file);
    }
}

static void on_save_file_clicked(GtkButton *button, gpointer user_data) {
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save Script");
    gtk_file_dialog_save(dialog, GTK_WINDOW(main_window), NULL, save_file_response, NULL);
}

// --- Attach/Injection Logic ---

struct attach_data {
    GtkButton *button;
};

struct label_update_data {
    GtkButton *button;
    char *new_label;
};

// Must be called on the main thread
static gboolean update_button_label(gpointer user_data) {
    struct label_update_data *data = user_data;
    gtk_button_set_label(data->button, data->new_label);
    g_free(data->new_label);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void* attach_thread_func(void *arg) {
    struct attach_data *data = arg;
    
    // Resolve Absolute Paths
    char *base_dir = get_dir_of_executable();
    char *so_path = g_build_filename(base_dir, "sober_test_inject.so", NULL);
    char *injector_path = g_build_filename(base_dir, "injector", NULL);
    g_free(base_dir);

    // Find PID
    pid_t pid = findPID("sober");
    if (pid == -1) {
        struct label_update_data *upd = g_new(struct label_update_data, 1);
        upd->button = data->button;
        upd->new_label = g_strdup("Attach (Not Found)");
        g_idle_add(update_button_label, upd);
        
        g_free(so_path); g_free(injector_path); g_free(data);
        return NULL;
    }

    g_print("Found Target PID: %d\nInjector: %s\nPayload: %s\n", pid, injector_path, so_path);

    // Prepare Spawn
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    gchar *pid_str = g_strdup_printf("%d", pid);
    gchar *argv[] = { injector_path, pid_str, so_path, NULL };

    gboolean success = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &stdout_buf, &stderr_buf, &exit_status, &error);

    g_free(pid_str);
    g_free(so_path);
    g_free(injector_path);

    struct label_update_data *upd = g_new(struct label_update_data, 1);
    upd->button = data->button;

    if (error) {
        g_printerr("Spawn Error: %s\n", error->message);
        upd->new_label = g_strdup("Attach (Sys Error)");
        g_error_free(error);
    } else if (!success || exit_status != 0) {
        g_printerr("Injection Failed (Exit: %d)\nStdOut: %s\nStdErr: %s\n", exit_status, stdout_buf, stderr_buf);
        upd->new_label = g_strdup("Attach (Failed)");
    } else {
        g_print("Injection Success!\n");
        upd->new_label = g_strdup("Attached!");
    }

    g_free(stdout_buf);
    g_free(stderr_buf);
    
    g_idle_add(update_button_label, upd);
    g_free(data);
    return NULL;
}

static void on_attach_clicked(GtkButton *button, gpointer user_data) {
    gtk_button_set_label(button, "Attaching...");
    struct attach_data *data = g_malloc(sizeof(struct attach_data));
    data->button = button;
    
    pthread_t thread;
    pthread_create(&thread, NULL, attach_thread_func, data);
    pthread_detach(thread);
}

// --- Application Activate ---

static void activate(GtkApplication *app, gpointer user_data) {
    main_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(main_window), "Atingle Injector");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 800, 400);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_window_set_child(GTK_WINDOW(main_window), grid);

    // --- Scrolled Window for Editor (The Fix) ---
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_grid_attach(GTK_GRID(grid), scrolled_window, 0, 0, 1, 1);

    editor = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor), GTK_WRAP_WORD_CHAR);
    // Add monospace font class for code look
    gtk_widget_add_css_class(editor, "monospace");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), editor);
    // --------------------------------------------

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_grid_attach(GTK_GRID(grid), button_box, 0, 1, 1, 1);

    GtkWidget *btn_execute = gtk_button_new_with_label("Execute");
    GtkWidget *btn_clear = gtk_button_new_with_label("Clear");
    GtkWidget *btn_open = gtk_button_new_with_label("Open File");
    GtkWidget *btn_save = gtk_button_new_with_label("Save File");
    GtkWidget *btn_attach = gtk_button_new_with_label("Attach");

    // CSS Styling classes
    gtk_widget_add_css_class(btn_execute, "suggested-action");
    gtk_widget_add_css_class(btn_attach, "destructive-action");

    gtk_box_append(GTK_BOX(button_box), btn_execute);
    gtk_box_append(GTK_BOX(button_box), btn_clear);
    gtk_box_append(GTK_BOX(button_box), btn_open);
    gtk_box_append(GTK_BOX(button_box), btn_save);
    gtk_box_append(GTK_BOX(button_box), btn_attach);

    g_signal_connect(btn_execute, "clicked", G_CALLBACK(on_execute_clicked), NULL);
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_clear_clicked), NULL);
    g_signal_connect(btn_open, "clicked", G_CALLBACK(on_open_file_clicked), NULL);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_file_clicked), NULL);
    g_signal_connect(btn_attach, "clicked", G_CALLBACK(on_attach_clicked), NULL);

    gtk_window_present(GTK_WINDOW(main_window));
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.atingle.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

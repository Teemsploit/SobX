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
        gchar *text = gtk_text_buffer_get_text(buffer, &

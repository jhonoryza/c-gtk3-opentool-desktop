/**
 * main.c — Config Opener for Sublime Text
 *
 * A GTK3 desktop application (pure C, C99/C11) for macOS that lists
 * configuration files and opens them with Sublime Text via fork()/execlp().
 *
 * Compile:
 *   gcc -std=c99 -Wall -Wextra -o config-opener main.c \
 *       $(pkg-config --cflags --libs gtk+-3.0)
 */

#include <gtk/gtk.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#define MAX_CONFIGS 7

typedef struct {
    const char *name;
    const char *path_template;   /* ~ will be expanded; NULL = use dynamic user path */
    int         use_dynamic_user; /* 1 = build path using $USER */
} ConfigItem;

static const ConfigItem configs[MAX_CONFIGS] = {
    {"opencode",       "~/.config/opencode/opencode.jsonc",                             0},
    {"gowails chatai", NULL,                                                             1},
    {"zshrc pre",      "~/.zshrc.pre-oh-my-zsh",                                        0},
    {"zshrc",          "~/.zshrc",                                                       0},
    {"tmux",           "~/.tmux.conf",                                                   0},
    {"ghossty",        "~/.config/ghostty/config",                                       0},
    {"zed",            "~/.config/zed/settings.json",                                    0},
};

/* ────────────────────────────────────────────────────────────────────────
 *  Path helpers
 * ──────────────────────────────────────────────────────────────────────── */

/**
 * Expand a path: replace a leading ~ with the user's home directory.
 * Returns a newly allocated string that the caller must free with g_free().
 */
static char *
expand_path(const char *path_template)
{
    const char *home;

    if (path_template == NULL || path_template[0] != '~')
        return g_strdup(path_template);

    home = g_get_home_dir();   /* GLib: uses $HOME or falls back to /etc/passwd */
    if (home == NULL)
        home = "/tmp";

    return g_strconcat(home, path_template + 1, NULL);
}

/**
 * Build the dynamic gowails-chatai path from the current username.
 * Returns a newly allocated string; caller must free with g_free().
 */
static char *
get_dynamic_user_path(void)
{
    const char *user = g_get_user_name();  /* GLib: uses $USER or /etc/passwd */
    if (user == NULL)
        user = "fajar";

    return g_strdup_printf(
        "/Users/%s/Library/Application Support/gowails-chatai-desktop/settings.json",
        user);
}

/**
 * Return the fully expanded path for a config item.
 * Caller must free the result with g_free().
 */
static char *
get_config_path(int index)
{
    if (index < 0 || index >= MAX_CONFIGS)
        return NULL;

    if (configs[index].use_dynamic_user)
        return get_dynamic_user_path();

    return expand_path(configs[index].path_template);
}

/* ────────────────────────────────────────────────────────────────────────
 *  Utility helpers
 * ──────────────────────────────────────────────────────────────────────── */

/**
 * Check whether a command exists and is executable somewhere in $PATH.
 */
static int
command_exists(const char *cmd)
{
    const char *path_env = g_getenv("PATH");
    if (path_env == NULL)
        return 0;

    gchar **dirs = g_strsplit(path_env, ":", 0);
    int found = 0;

    for (int i = 0; dirs[i] != NULL; i++) {
        gchar *full_path = g_build_filename(dirs[i], cmd, NULL);
        if (g_file_test(full_path, G_FILE_TEST_IS_EXECUTABLE)) {
            found = 1;
            g_free(full_path);
            break;
        }
        g_free(full_path);
    }

    g_strfreev(dirs);
    return found;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Callbacks
 * ──────────────────────────────────────────────────────────────────────── */

/**
 * "Open with Sublime Text" button callback.
 * Expands the path, validates 'subl' availability, checks file existence,
 * then fork()/execlp() to launch Sublime Text.
 */
static void
on_open_clicked(GtkButton *button, gpointer user_data)
{
    int   index     = GPOINTER_TO_INT(user_data);
    char *full_path = get_config_path(index);
    if (full_path == NULL)
        return;

    /* ---- Check that 'subl' is available in PATH ---- */
    if (!command_exists("subl")) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Command 'subl' not found.\n\n"
            "Please ensure Sublime Text is installed and the 'subl' command "
            "line tool is available in your PATH.\n\n"
            "To install the subl command:\n"
            "  ln -s /Applications/Sublime\\ Text.app/Contents/SharedSupport/"
            "bin/subl /usr/local/bin/subl");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(full_path);
        return;
    }

    /* ---- Check if the file exists (warning only) ---- */
    if (!g_file_test(full_path, G_FILE_TEST_IS_REGULAR)) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "File not found:\n%s\n\n"
            "Sublime Text may create a new file or show an error.",
            full_path);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    /* ---- Fork and exec subl ---- */
    pid_t pid = fork();

    if (pid == 0) {
        /* Child process: exec subl.
         *
         * The file path is passed as a single argv element (argv[1]).
         * Since execlp() does NOT go through a shell, spaces in the path
         * are preserved correctly — no quoting or escaping needed. */
        execlp("subl", "subl", full_path, (char *)NULL);

        /* If execlp returns, it failed */
        fprintf(stderr,
                "[config-opener] Failed to exec 'subl' with path '%s': %s\n",
                full_path, strerror(errno));
        _exit(1);

    } else if (pid < 0) {
        /* Fork failed — show error dialog */
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Failed to launch Sublime Text:\n%s",
            strerror(errno));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    /* Parent continues — child runs independently */
    g_free(full_path);
}

/**
 * Window "destroy" event → quit the GTK main loop.
 */
static void
on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    gtk_main_quit();
}

/* ────────────────────────────────────────────────────────────────────────
 *  Widget creation helpers
 * ──────────────────────────────────────────────────────────────────────── */

/**
 * Create a bold header label (for the grid column headers).
 */
static GtkWidget *
create_header_label(const char *text)
{
    GtkWidget *label = gtk_label_new(NULL);
    gchar     *markup = g_strdup_printf("<b>%s</b>", text);

    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);

    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    return label;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Entry point
 * ──────────────────────────────────────────────────────────────────────── */

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    /* Auto-reap child processes so we don't leave zombies */
    signal(SIGCHLD, SIG_IGN);

    /* ── Main window ── */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),
                         "Config Opener - Sublime Text Launcher");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 300);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    /* ── Root vertical box with padding ── */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 12);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    /* ── Title bar ── */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Configuration Files</b>");
    gtk_box_pack_start(GTK_BOX(main_box), title, FALSE, FALSE, 0);

    /* ── Separator ── */
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), separator, FALSE, FALSE, 2);

    /* ── Scrolled window ── */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled, TRUE, TRUE, 0);

    /* ── Grid: 2 columns (config name | action button) ── */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_add(GTK_CONTAINER(scrolled), grid);

    /* Column headers */
    GtkWidget *hdr_name   = create_header_label("Config Name");
    GtkWidget *hdr_action = create_header_label("Action");
    gtk_grid_attach(GTK_GRID(grid), hdr_name,   0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), hdr_action, 1, 0, 1, 1);

    /* Separator row under headers */
    GtkWidget *grid_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach(GTK_GRID(grid), grid_sep, 0, 1, 2, 1);

    /* Config item rows */
    for (int i = 0; i < MAX_CONFIGS; i++) {
        int row = i + 2;

        /* Config name label (left-aligned, ellipsized if too long) */
        GtkWidget *name = gtk_label_new(configs[i].name);
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_widget_set_valign(name, GTK_ALIGN_CENTER);
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        gtk_grid_attach(GTK_GRID(grid), name, 0, row, 1, 1);

        /* "Open with Sublime Text" button */
        GtkWidget *btn = gtk_button_new_with_label("Open with Sublime Text");
        g_signal_connect(btn, "clicked",
                         G_CALLBACK(on_open_clicked),
                         GINT_TO_POINTER(i));
        gtk_widget_set_halign(btn, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 1, 1);
    }

    /* ── Show everything and enter the main loop ── */
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}

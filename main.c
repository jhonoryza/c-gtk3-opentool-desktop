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

/* ────────────────────────────────────────────────────────────────────────
 *  Data
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *path_template;    /* ~ → $HOME ; NULL = dynamic via $USER */
    int         use_dynamic_user;
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
 *  CSS Stylesheet
 * ──────────────────────────────────────────────────────────────────────── */

static const char *APP_CSS =
    /* ── Root window ── */
    "window {"
    "  background-color: #f5f5f7;"
    "}"

    /* ── Header ── */
    ".header-box {"
    "  background-color: #ffffff;"
    "  border-bottom: 1px solid #d2d2d7;"
    "  padding: 12px 16px;"
    "}"
    ".title-label {"
    "  font-size: 15px;"
    "  font-weight: bold;"
    "  color: #1d1d1f;"
    "}"
    ".subtitle-label {"
    "  font-size: 11px;"
    "  color: #86868b;"
    "  margin-top: 2px;"
    "}"

    /* ── List box ── */
    "list {"
    "  background-color: #ffffff;"
    "}"

    /* ── List rows ── */
    "list row {"
    "  background-color: #ffffff;"
    "  border-bottom: 1px solid #f0f0f2;"
    "  padding: 0;"
    "}"
    "list row:nth-child(even) {"
    "  background-color: #fafafc;"
    "}"
    "list row:hover {"
    "  background-color: #e8f0fe;"
    "  transition: background-color 150ms ease;"
    "}"

    /* ── Config name label ── */
    ".config-name {"
    "  font-size: 13px;"
    "  color: #1d1d1f;"
    "  padding: 8px 4px 8px 16px;"
    "}"

    /* ── Open button ── */
    ".open-button {"
    "  background-image: linear-gradient(to bottom, #007aff, #0062cc);"
    "  color: #ffffff;"
    "  border: none;"
    "  border-radius: 5px;"
    "  padding: 5px 14px;"
    "  font-size: 12px;"
    "  font-weight: 500;"
    "  margin: 4px 16px 4px 4px;"
    "}"
    ".open-button:hover {"
    "  background-image: linear-gradient(to bottom, #0062cc, #0055b3);"
    "}"
    ".open-button:active {"
    "  background-image: linear-gradient(to bottom, #0055b3, #004499);"
    "}"

    /* ── Status bar ── */
    ".status-bar {"
    "  font-size: 11px;"
    "  color: #86868b;"
    "  background-color: #ffffff;"
    "  border-top: 1px solid #d2d2d7;"
    "  padding: 5px 16px;"
    "}"

    /* ── Scrollbar ── */
    "scrollbar {"
    "  background-color: transparent;"
    "  border: none;"
    "}"
    "scrollbar slider {"
    "  background-color: rgba(0,0,0,0.16);"
    "  border-radius: 4px;"
    "  min-width: 6px;"
    "}"
    "scrollbar slider:hover {"
    "  background-color: rgba(0,0,0,0.28);"
    "}"

    /* ── Scrolled window ── */
    "scrolledwindow undershoot,"
    "scrolledwindow overshoot {"
    "  background-color: transparent;"
    "}"
    ;

/* ────────────────────────────────────────────────────────────────────────
 *  Path helpers
 * ──────────────────────────────────────────────────────────────────────── */

static char *
expand_path(const char *path_template)
{
    const char *home;

    if (path_template == NULL || path_template[0] != '~')
        return g_strdup(path_template);

    home = g_get_home_dir();
    if (home == NULL)
        home = "/tmp";

    return g_strconcat(home, path_template + 1, NULL);
}

static char *
get_dynamic_user_path(void)
{
    const char *user = g_get_user_name();
    if (user == NULL)
        user = "fajar";

    return g_strdup_printf(
        "/Users/%s/Library/Application Support/gowails-chatai-desktop/settings.json",
        user);
}

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

static void
on_open_clicked(GtkButton *button, gpointer user_data)
{
    int   index     = GPOINTER_TO_INT(user_data);
    char *full_path = get_config_path(index);
    if (full_path == NULL)
        return;

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

    pid_t pid = fork();

    if (pid == 0) {
        execlp("subl", "subl", full_path, (char *)NULL);
        fprintf(stderr,
                "[config-opener] Failed to exec 'subl' with path '%s': %s\n",
                full_path, strerror(errno));
        _exit(1);
    } else if (pid < 0) {
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

    g_free(full_path);
}

static void
on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    gtk_main_quit();
}

/* ────────────────────────────────────────────────────────────────────────
 *  Apply CSS
 * ──────────────────────────────────────────────────────────────────────── */

static void
apply_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    GError         *error   = NULL;

    gtk_css_provider_load_from_data(provider, APP_CSS, -1, &error);
    if (error != NULL) {
        g_warning("CSS load error: %s", error->message);
        g_error_free(error);
    }

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

/* ────────────────────────────────────────────────────────────────────────
 *  Build a single list row
 * ──────────────────────────────────────────────────────────────────────── */

static GtkWidget *
create_config_row(int index)
{
    /* Horizontal box: [ label (expand) | button ] */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    /* Config name */
    GtkWidget *name = gtk_label_new(configs[index].name);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_widget_set_valign(name, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(name, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(name), "config-name");
    gtk_box_pack_start(GTK_BOX(hbox), name, TRUE, TRUE, 0);

    /* Open button */
    GtkWidget *btn = gtk_button_new_with_label("Open in Sublime Text");
    g_signal_connect(btn, "clicked",
                     G_CALLBACK(on_open_clicked),
                     GINT_TO_POINTER(index));
    gtk_style_context_add_class(
        gtk_widget_get_style_context(btn), "open-button");
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);

    /* Tooltip with the full path */
    char *full_path = get_config_path(index);
    if (full_path) {
        gtk_widget_set_tooltip_text(btn, full_path);
        g_free(full_path);
    }

    return hbox;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Entry point
 * ──────────────────────────────────────────────────────────────────────── */

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    /* Apply stylesheet */
    apply_css();

    /* Auto-reap child processes */
    signal(SIGCHLD, SIG_IGN);

    /* ── Main window ── */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),
                         "Config Opener - Sublime Text Launcher");
    gtk_window_set_default_size(GTK_WINDOW(window), 540, 350);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    /* ── Root vertical box (no spacing — handled by children) ── */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    /* ── Header ── */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(header), "header-box");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span font_weight=\"bold\" size=\"12000\">"
                         "Configuration Files</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(title), "title-label");
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

    gchar *sub_text = g_strdup_printf(
        "%d config files  ·  click to open with Sublime Text", MAX_CONFIGS);
    GtkWidget *subtitle = gtk_label_new(sub_text);
    g_free(sub_text);
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(subtitle), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(header), subtitle, FALSE, FALSE, 0);

    /* ── Scrolled window ── */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    /* ── List box for config items ── */
    GtkWidget *listbox = gtk_list_box_new();
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(listbox), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), listbox);

    for (int i = 0; i < MAX_CONFIGS; i++) {
        GtkWidget *row = create_config_row(i);
        gtk_container_add(GTK_CONTAINER(listbox), row);
    }

    /* ── Status bar ── */
    gchar *status_text = g_strdup_printf(
        "✓  %d configuration files  ·  Sublime Text Launcher", MAX_CONFIGS);
    GtkWidget *status = gtk_label_new(status_text);
    g_free(status_text);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(status), "status-bar");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), status, FALSE, FALSE, 0);

    /* ── Show and run ── */
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}

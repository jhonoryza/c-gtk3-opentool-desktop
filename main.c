/**
 * main.c -- Opentool Desktop
 *
 * A GTK3 desktop application (pure C, C11) for Linux with tabs:
 *   Home, OpenCode, Claude, SSH, Terminal, Chat Web, Kimchi, Mimo,
 *   Freebuff, Port Monitor, Config Opener, Claude Switcher, Command Palette.
 *
 * Compile:
 *   make
 *   gcc -std=c11 -Wall -Wextra -o opentool main.c \
 *       $(pkg-config --cflags --libs gtk+-3.0 sqlite3 vte-2.91 webkit2gtk-4.1)
 */

#include <gtk/gtk.h>
#include <sqlite3.h>
#include <vte/vte.h>
#include <webkit2/webkit2.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>

/* ────────────────────────────────────────────────────────────────────────
 *  Domain types
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    int   id;
    char *name;
    char *path;          /* may start with '~'; expanded at open time */
} ConfigEntry;

typedef struct {
    int   id;
    char *name;
    char *api_key;
    char *base_url;
    int   is_active;
} Account;

typedef struct {
    char *name;
    int   enabled;
} Plugin;

static sqlite3       *g_db                = NULL;
static GtkWidget     *g_config_list_box   = NULL;
static GtkWidget     *g_account_list_box  = NULL;
static GtkWidget     *g_plugins_list_box  = NULL;
static GtkWidget     *g_main_window       = NULL;
static GtkWidget     *g_status_label      = NULL;
static GtkWidget     *g_theme_btn         = NULL;
static GtkWidget     *g_notebook          = NULL;
static GtkCssProvider *g_css_provider     = NULL;
static gboolean       g_dark_mode         = FALSE;

/* ────────────────────────────────────────────────────────────────────────
 *  CSS Stylesheet — split into theme tokens + body so apply_theme() can
 *  swap colors at runtime without re-emitting the full sheet.
 * ──────────────────────────────────────────────────────────────────────── */

/* Light tokens — Apple-style light surfaces. */
static const char *CSS_VARS_LIGHT =
    "@define-color c_bg_window      #f5f5f7;"
    "@define-color c_bg_header      #ececef;"
    "@define-color c_bg_card        #ffffff;"
    "@define-color c_bg_alt         #fafafc;"
    "@define-color c_bg_hover       #e8f0fe;"
    "@define-color c_border         #d2d2d7;"
    "@define-color c_border_soft    #f0f0f2;"
    "@define-color c_text_primary   #1d1d1f;"
    "@define-color c_text_secondary #86868b;"
    "@define-color c_text_tab_off   #515154;"
    "@define-color c_accent         #007aff;"
    "@define-color c_accent_d1      #0062cc;"
    "@define-color c_accent_d2      #0055b3;"
    "@define-color c_accent_d3      #004499;"
    "@define-color c_success        #34c759;"
    "@define-color c_success_d1     #28a745;"
    "@define-color c_success_d2     #218838;"
    "@define-color c_danger         #ff3b30;"
    "@define-color c_danger_d1      #d70015;"
    "@define-color c_danger_d2      #b00010;"
    "@define-color c_btn_sec_bg     #f0f0f2;"
    "@define-color c_btn_sec_bg_h   #e5e5e8;"
    "@define-color c_btn_neutral_bg #ffffff;"
    "@define-color c_dot_off        #d2d2d7;"
    "@define-color c_disabled_bg    #d2d2d7;"
    "@define-color c_disabled_text  #86868b;"
    "@define-color c_scroll_thumb   rgba(0,0,0,0.16);"
    "@define-color c_scroll_thumb_h rgba(0,0,0,0.28);"
    ;

/* Dark tokens — Apple-style dark surfaces. */
static const char *CSS_VARS_DARK =
    "@define-color c_bg_window      #1c1c1e;"
    "@define-color c_bg_header      #2c2c2e;"
    "@define-color c_bg_card        #242426;"
    "@define-color c_bg_alt         #2a2a2c;"
    "@define-color c_bg_hover       #2f3a4a;"
    "@define-color c_border         #3a3a3c;"
    "@define-color c_border_soft    #2a2a2c;"
    "@define-color c_text_primary   #f5f5f7;"
    "@define-color c_text_secondary #98989d;"
    "@define-color c_text_tab_off   #98989d;"
    "@define-color c_accent         #0a84ff;"
    "@define-color c_accent_d1      #0066cc;"
    "@define-color c_accent_d2      #0055b3;"
    "@define-color c_accent_d3      #004499;"
    "@define-color c_success        #30d158;"
    "@define-color c_success_d1     #28a745;"
    "@define-color c_success_d2     #1f7a37;"
    "@define-color c_danger         #ff453a;"
    "@define-color c_danger_d1      #d70015;"
    "@define-color c_danger_d2      #b00010;"
    "@define-color c_btn_sec_bg     #3a3a3c;"
    "@define-color c_btn_sec_bg_h   #48484a;"
    "@define-color c_btn_neutral_bg #2c2c2e;"
    "@define-color c_dot_off        #48484a;"
    "@define-color c_disabled_bg    #3a3a3c;"
    "@define-color c_disabled_text  #98989d;"
    "@define-color c_scroll_thumb   rgba(255,255,255,0.20);"
    "@define-color c_scroll_thumb_h rgba(255,255,255,0.32);"
    ;

static const char *CSS_BODY =
    /* ── Root window ── */
    "window {"
    "  background-color: @c_bg_window;"
    "  color: @c_text_primary;"
    "}"

    /* ── Notebook tabs ── */
    "notebook {"
    "  background-color: @c_bg_window;"
    "}"
    "notebook header {"
    "  background-color: @c_bg_header;"
    "  border-bottom: 1px solid @c_border;"
    "}"
    "notebook tab {"
    "  padding: 8px 18px;"
    "  background-color: transparent;"
    "  color: @c_text_tab_off;"
    "  font-size: 12px;"
    "  font-weight: 500;"
    "  border: none;"
    "}"
    "notebook tab:checked {"
    "  background-color: @c_bg_card;"
    "  color: @c_text_primary;"
    "  border-bottom: 2px solid @c_accent;"
    "}"

    /* ── Header ── */
    ".header-box {"
    "  background-color: @c_bg_card;"
    "  border-bottom: 1px solid @c_border;"
    "  padding: 12px 16px;"
    "}"
    ".title-label {"
    "  font-size: 15px;"
    "  font-weight: bold;"
    "  color: @c_text_primary;"
    "}"
    ".subtitle-label {"
    "  font-size: 11px;"
    "  color: @c_text_secondary;"
    "  margin-top: 2px;"
    "}"

    /* ── List box ── */
    "list {"
    "  background-color: @c_bg_card;"
    "  color: @c_text_primary;"
    "}"
    "list row {"
    "  background-color: @c_bg_card;"
    "  border-bottom: 1px solid @c_border_soft;"
    "  padding: 0;"
    "  color: @c_text_primary;"
    "}"
    "list row:nth-child(even) {"
    "  background-color: @c_bg_alt;"
    "}"
    "list row:hover {"
    "  background-color: @c_bg_hover;"
    "  transition: background-color 150ms ease;"
    "}"

    /* ── Config name label ── */
    ".config-name {"
    "  font-size: 13px;"
    "  color: @c_text_primary;"
    "  padding: 8px 4px 8px 16px;"
    "}"

    /* ── Account row labels ── */
    ".account-name {"
    "  font-size: 13px;"
    "  font-weight: 600;"
    "  color: @c_text_primary;"
    "}"
    ".account-meta {"
    "  font-size: 11px;"
    "  color: @c_text_secondary;"
    "}"

    /* ── Active indicator dot ── */
    ".dot-active {"
    "  background-color: @c_success;"
    "  border-radius: 7px;"
    "  min-width: 12px;"
    "  min-height: 12px;"
    "}"
    ".dot-inactive {"
    "  background-color: @c_dot_off;"
    "  border-radius: 7px;"
    "  min-width: 12px;"
    "  min-height: 12px;"
    "}"

    /* ── Open button (blue) ── */
    ".open-button {"
    "  background-image: linear-gradient(to bottom, @c_accent, @c_accent_d1);"
    "  color: #ffffff;"
    "  border: none;"
    "  border-radius: 5px;"
    "  padding: 5px 14px;"
    "  font-size: 12px;"
    "  font-weight: 500;"
    "  margin: 4px 4px 4px 4px;"
    "}"
    ".open-button:hover {"
    "  background-image: linear-gradient(to bottom, @c_accent_d1, @c_accent_d2);"
    "}"
    ".open-button:active {"
    "  background-image: linear-gradient(to bottom, @c_accent_d2, @c_accent_d3);"
    "}"

    /* ── Activate button (green) ── */
    ".activate-button {"
    "  background-image: linear-gradient(to bottom, @c_success, @c_success_d1);"
    "  color: #ffffff;"
    "  border: none;"
    "  border-radius: 5px;"
    "  padding: 5px 14px;"
    "  font-size: 12px;"
    "  font-weight: 500;"
    "  margin: 4px;"
    "}"
    ".activate-button:hover {"
    "  background-image: linear-gradient(to bottom, @c_success_d1, @c_success_d2);"
    "}"
    ".activate-button:disabled {"
    "  background-image: none;"
    "  background-color: @c_disabled_bg;"
    "  color: @c_disabled_text;"
    "}"

    /* ── Edit button (gray) ── */
    ".edit-button {"
    "  background-color: @c_btn_sec_bg;"
    "  color: @c_text_primary;"
    "  border: 1px solid @c_border;"
    "  border-radius: 5px;"
    "  padding: 5px 12px;"
    "  font-size: 12px;"
    "  margin: 4px 2px;"
    "}"
    ".edit-button:hover {"
    "  background-color: @c_btn_sec_bg_h;"
    "}"

    /* ── Delete button (red) ── */
    ".delete-button {"
    "  background-image: linear-gradient(to bottom, @c_danger, @c_danger_d1);"
    "  color: #ffffff;"
    "  border: none;"
    "  border-radius: 5px;"
    "  padding: 5px 12px;"
    "  font-size: 12px;"
    "  margin: 4px 2px;"
    "}"
    ".delete-button:hover {"
    "  background-image: linear-gradient(to bottom, @c_danger_d1, @c_danger_d2);"
    "}"

    /* ── Add button (top of switcher) ── */
    ".add-button {"
    "  background-image: linear-gradient(to bottom, @c_accent, @c_accent_d1);"
    "  color: #ffffff;"
    "  border: none;"
    "  border-radius: 5px;"
    "  padding: 6px 14px;"
    "  font-size: 12px;"
    "  font-weight: 500;"
    "}"

    /* ── Reset / Plugins toolbar buttons ── */
    ".reset-button, .plugins-button {"
    "  background-color: @c_btn_neutral_bg;"
    "  color: @c_text_primary;"
    "  border: 1px solid @c_border;"
    "  border-radius: 5px;"
    "  padding: 6px 14px;"
    "  font-size: 12px;"
    "  font-weight: 500;"
    "}"
    ".reset-button:hover, .plugins-button:hover {"
    "  background-color: @c_btn_sec_bg;"
    "}"

    /* ── Theme toggle button (sits in the notebook tab bar) ── */
    ".theme-button {"
    "  background-color: transparent;"
    "  color: @c_text_primary;"
    "  border: 1px solid @c_border;"
    "  border-radius: 5px;"
    "  padding: 4px 10px;"
    "  margin: 4px 10px 4px 4px;"
    "  font-size: 12px;"
    "}"
    ".theme-button:hover {"
    "  background-color: @c_btn_sec_bg;"
    "}"

    /* ── Empty state ── */
    ".empty-label {"
    "  font-size: 13px;"
    "  color: @c_text_secondary;"
    "  padding: 36px;"
    "}"

    /* ── Status bar ── */
    ".status-bar {"
    "  font-size: 11px;"
    "  color: @c_text_secondary;"
    "  background-color: @c_bg_card;"
    "  border-top: 1px solid @c_border;"
    "  padding: 5px 16px;"
    "}"

    /* ── Entries (text input) ── */
    "entry {"
    "  background-image: none;"
    "  background-color: @c_bg_card;"
    "  color: @c_text_primary;"
    "  caret-color: @c_text_primary;"
    "  border: 1px solid @c_border;"
    "  border-radius: 4px;"
    "  padding: 4px 6px;"
    "}"
    "entry selection {"
    "  background-color: @c_accent;"
    "  color: #ffffff;"
    "}"

    /* ── Dialogs ── */
    "dialog {"
    "  background-color: @c_bg_window;"
    "  color: @c_text_primary;"
    "}"
    "dialog label,"
    "messagedialog label {"
    "  color: @c_text_primary;"
    "}"
    "messagedialog {"
    "  background-color: @c_bg_window;"
    "  color: @c_text_primary;"
    "}"

    /* ── Scrollbar ── */
    "scrollbar {"
    "  background-color: transparent;"
    "  border: none;"
    "}"
    "scrollbar slider {"
    "  background-color: @c_scroll_thumb;"
    "  border-radius: 4px;"
    "  min-width: 6px;"
    "}"
    "scrollbar slider:hover {"
    "  background-color: @c_scroll_thumb_h;"
    "}"
    "scrolledwindow undershoot,"
    "scrolledwindow overshoot {"
    "  background-color: transparent;"
    "}"

    /* ── Home dashboard cards ── */
    ".home-card {"
    "  background-color: @c_bg_card;"
    "  border: 1px solid @c_border;"
    "  border-radius: 8px;"
    "  padding: 16px;"
    "}"
    ".home-card:hover {"
    "  background-color: @c_bg_hover;"
    "  border-color: @c_accent;"
    "  transition: all 150ms ease;"
    "}"
    ".home-card-icon {"
    "  font-size: 28px;"
    "  color: @c_accent;"
    "  min-height: 36px;"
    "}"
    ".home-card-title {"
    "  font-size: 13px;"
    "  font-weight: 600;"
    "  color: @c_text_primary;"
    "}"
    ".home-card-desc {"
    "  font-size: 11px;"
    "  color: @c_text_secondary;"
    "  margin-top: 2px;"
    "}"

    /* ── Home keyboard shortcut badges ── */
    ".home-shortcut-key {"
    "  background-color: @c_btn_sec_bg;"
    "  border: 1px solid @c_border;"
    "  border-radius: 4px;"
    "  padding: 2px 8px;"
    "  font-size: 11px;"
    "  font-family: monospace;"
    "  color: @c_text_primary;"
    "}"
    ".home-shortcut-desc {"
    "  font-size: 11px;"
    "  color: @c_text_secondary;"
    "}"
    ".home-section-title {"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  color: @c_text_primary;"
    "  margin-top: 16px;"
    "  margin-bottom: 8px;"
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
    const char *home = g_get_home_dir();
    if (home == NULL) home = "/tmp";
    return g_strdup_printf(
        "%s/.config/gowails-chatai-desktop/settings.json",
        home);
}

static char *
get_app_data_dir(void)
{
    const char *xdg = g_getenv("XDG_DATA_HOME");
    if (xdg != NULL && *xdg != '\0')
        return g_strdup_printf("%s/opentool", xdg);
    const char *home = g_get_home_dir();
    if (home == NULL) home = "/tmp";
    return g_strdup_printf("%s/.local/share/opentool", home);
}

static char *
get_db_path(void)
{
    char *dir = get_app_data_dir();
    char *path = g_strdup_printf("%s/accounts.db", dir);
    g_free(dir);
    return path;
}

static char *
get_claude_settings_path(void)
{
    const char *home = g_get_home_dir();
    if (home == NULL) home = "/tmp";
    return g_strdup_printf("%s/.claude/settings.json", home);
}

static char *
get_claude_settings_backup_path(void)
{
    const char *home = g_get_home_dir();
    if (home == NULL) home = "/tmp";
    return g_strdup_printf("%s/.claude/settings.json.bak", home);
}

/* Directory containing the current executable. Caller frees. */
static char *
get_exe_dir(void)
{
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;
    buf[n] = 0;
    return g_path_get_dirname(buf);
}

/* Search for logo-512.png in: same dir as exe, share dirs.
 * Set as default icon so every GtkWindow picks it up automatically. */
static void
setup_app_icon(void)
{
    static const char *const rel_candidates[] = {
        "logo-512.png",
        "../share/opentool/logo-512.png",
        NULL,
    };
    static const char *const abs_candidates[] = {
        "/usr/local/share/opentool/logo-512.png",
        "/usr/share/opentool/logo-512.png",
        NULL,
    };

    char *found   = NULL;
    char *exe_dir = get_exe_dir();
    if (exe_dir != NULL) {
        for (int i = 0; rel_candidates[i] != NULL; i++) {
            char *p = g_build_filename(exe_dir, rel_candidates[i], NULL);
            if (g_file_test(p, G_FILE_TEST_IS_REGULAR)) {
                found = p;
                break;
            }
            g_free(p);
        }
    }
    if (found == NULL) {
        for (int i = 0; abs_candidates[i] != NULL; i++) {
            if (g_file_test(abs_candidates[i], G_FILE_TEST_IS_REGULAR)) {
                found = g_strdup(abs_candidates[i]);
                break;
            }
        }
    }

    if (found != NULL) {
        GError *err = NULL;
        if (!gtk_window_set_default_icon_from_file(found, &err)) {
            fprintf(stderr, "[icon] %s: %s\n",
                    found, err ? err->message : "unknown");
            if (err) g_error_free(err);
        }
        g_free(found);
    }
    g_free(exe_dir);
}

/* mkdir -p */
static int
ensure_dir(const char *path)
{
    if (path == NULL || *path == 0) return -1;
    char *copy = g_strdup(path);
    int   len  = strlen(copy);
    for (int i = 1; i < len; i++) {
        if (copy[i] == '/') {
            copy[i] = 0;
            mkdir(copy, 0755);
            copy[i] = '/';
        }
    }
    mkdir(copy, 0755);
    g_free(copy);
    return 0;
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

static const char *
get_editor(void)
{
    const char *editor = g_getenv("EDITOR");
    if (editor != NULL && *editor != '\0') return editor;
    if (command_exists("xdg-open")) return "xdg-open";
    if (command_exists("gedit"))    return "gedit";
    if (command_exists("nano"))     return "nano";
    return NULL;
}

static void
launch_editor(GtkWidget *parent, const char *path)
{
    const char *editor = get_editor();
    if (editor == NULL) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(parent)),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "No editor found.\n\n"
            "Set the $EDITOR environment variable or install xdg-open.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execlp(editor, editor, path, (char *)NULL);
        fprintf(stderr, "[opentool] exec %s failed for '%s': %s\n",
                editor, path, strerror(errno));
        _exit(1);
    } else if (pid < 0) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(parent)),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Failed to launch editor:\n%s",
            strerror(errno));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

static void
show_error(GtkWidget *parent, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    GtkWidget *dialog = gtk_message_dialog_new(
        parent ? GTK_WINDOW(gtk_widget_get_toplevel(parent)) : NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(msg);
}

static void
set_status(const char *fmt, ...)
{
    if (g_status_label == NULL) return;
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(g_status_label), msg);
    g_free(msg);
}

/* ────────────────────────────────────────────────────────────────────────
 *  Keyboard shortcuts
 *
 *  Ctrl+W closes current window (or Ctrl+Q quits app).
 *  Ctrl+P opens command palette (handled in main).
 * ──────────────────────────────────────────────────────────────────────── */

static gboolean
on_window_key_press(GtkWidget *window, GdkEventKey *event, gpointer data)
{
    (void)data;

    guint mask = gtk_accelerator_get_default_mod_mask();
    if (!(event->state & GDK_CONTROL_MASK))
        return FALSE;

    guint key = gdk_keyval_to_lower(event->keyval);

    if (key == GDK_KEY_w && (mask & GDK_CONTROL_MASK) == GDK_CONTROL_MASK) {
        gtk_window_close(GTK_WINDOW(window));
        return TRUE;
    }
    if (key == GDK_KEY_q && (mask & GDK_CONTROL_MASK) == GDK_CONTROL_MASK) {
        gtk_main_quit();
        return TRUE;
    }
    if (key == GDK_KEY_p && (mask & GDK_CONTROL_MASK) == GDK_CONTROL_MASK) {
        palette_show();
        return TRUE;
    }
    if (key == GDK_KEY_k && (mask & GDK_CONTROL_MASK) == GDK_CONTROL_MASK) {
        if (g_notebook) gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), 0);
        return TRUE;
    }

    return FALSE;
}

static void
install_shortcuts(GtkWidget *window)
{
    g_signal_connect(window, "key-press-event",
                     G_CALLBACK(on_window_key_press), NULL);
}

/* ────────────────────────────────────────────────────────────────────────
 *  JSON helpers
 * ──────────────────────────────────────────────────────────────────────── */

static char *build_plugins_json_block(void);   /* fwd: defined after DB */

/* Escape a string for use inside JSON double quotes. */
static char *
json_escape(const char *s)
{
    if (s == NULL) return g_strdup("");
    GString *out = g_string_new(NULL);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '\\': g_string_append(out, "\\\\"); break;
            case '"':  g_string_append(out, "\\\"");  break;
            case '\n': g_string_append(out, "\\n");   break;
            case '\r': g_string_append(out, "\\r");   break;
            case '\t': g_string_append(out, "\\t");   break;
            case '\b': g_string_append(out, "\\b");   break;
            case '\f': g_string_append(out, "\\f");   break;
            default:
                if (c < 0x20)
                    g_string_append_printf(out, "\\u%04x", c);
                else
                    g_string_append_c(out, c);
        }
    }
    return g_string_free(out, FALSE);
}

/* Build the settings.json content for an account. */
static char *
build_settings_json(const Account *a)
{
    char *esc_key = json_escape(a->api_key);
    char *esc_url = json_escape(a->base_url);

    char *helper = g_strdup_printf("echo '%s'", esc_key);
    char *esc_helper = json_escape(helper);
    g_free(helper);

    char *plugins = build_plugins_json_block();
    char *json = g_strdup_printf(
        "{\n"
        "  \"apiKeyHelper\": \"%s\",\n"
        "  \"env\": {\n"
        "    \"ANTHROPIC_API_KEY\": \"%s\",\n"
        "    \"ANTHROPIC_BASE_URL\": \"%s\",\n"
        "    \"CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC\": \"1\"\n"
        "  },\n"
        "  \"permissions\": {\n"
        "    \"allow\": [],\n"
        "    \"deny\": []\n"
        "  },\n"
        "  \"model\": \"sonnet\",\n"
        "  \"effortLevel\": \"high\",\n"
        "  \"theme\": \"dark\",\n"
        "  \"enabledPlugins\": %s\n"
        "}\n",
        esc_helper, esc_key, esc_url, plugins);

    g_free(plugins);
    g_free(esc_key);
    g_free(esc_url);
    g_free(esc_helper);
    return json;
}

/* Build the reset settings.json (no api key / base url) using the current
 * dynamic plugin list. */
static char *
build_reset_settings_json(void)
{
    char *plugins = build_plugins_json_block();
    char *json = g_strdup_printf(
        "{\n"
        "  \"permissions\": {\n"
        "    \"allow\": [],\n"
        "    \"deny\": []\n"
        "  },\n"
        "  \"model\": \"sonnet\",\n"
        "  \"effortLevel\": \"high\",\n"
        "  \"enabledPlugins\": %s,\n"
        "  \"theme\": \"dark\"\n"
        "}\n",
        plugins);
    g_free(plugins);
    return json;
}

/* ────────────────────────────────────────────────────────────────────────
 *  SQLite — Database
 * ──────────────────────────────────────────────────────────────────────── */

static void
account_free(Account *a)
{
    if (a == NULL) return;
    g_free(a->name);
    g_free(a->api_key);
    g_free(a->base_url);
    g_free(a);
}

static int
db_init(void)
{
    char *dir = get_app_data_dir();
    ensure_dir(dir);
    g_free(dir);

    char *path = get_db_path();
    int rc = sqlite3_open(path, &g_db);
    g_free(path);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] open failed: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS accounts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL UNIQUE,"
        "  api_key TEXT NOT NULL,"
        "  base_url TEXT NOT NULL,"
        "  is_active INTEGER NOT NULL DEFAULT 0,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS plugins ("
        "  name TEXT PRIMARY KEY,"
        "  enabled INTEGER NOT NULL DEFAULT 1,"
        "  sort_order INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS configs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  path TEXT NOT NULL,"
        "  sort_order INTEGER NOT NULL DEFAULT 0,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");";

    char *err = NULL;
    rc = sqlite3_exec(g_db, schema, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] schema failed: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    /* Seed default plugins if table is empty. */
    sqlite3_stmt *cnt = NULL;
    int count = 0;
    if (sqlite3_prepare_v2(g_db, "SELECT COUNT(*) FROM plugins;",
                           -1, &cnt, NULL) == SQLITE_OK) {
        if (sqlite3_step(cnt) == SQLITE_ROW)
            count = sqlite3_column_int(cnt, 0);
        sqlite3_finalize(cnt);
    }
    if (count == 0) {
        sqlite3_exec(g_db,
            "INSERT INTO plugins (name, enabled, sort_order) VALUES "
            "  ('swift-lsp@claude-plugins-official', 1, 0),"
            "  ('gopls-lsp@claude-plugins-official', 1, 1);",
            NULL, NULL, NULL);
    }

    /* Seed default configs if table is empty. */
    count = 0;
    cnt = NULL;
    if (sqlite3_prepare_v2(g_db, "SELECT COUNT(*) FROM configs;",
                           -1, &cnt, NULL) == SQLITE_OK) {
        if (sqlite3_step(cnt) == SQLITE_ROW)
            count = sqlite3_column_int(cnt, 0);
        sqlite3_finalize(cnt);
    }
    if (count == 0) {
        static const struct {
            const char *name;
            const char *path;        /* NULL = resolve via $USER */
        } seeds[] = {
            {"opencode",       "~/.config/opencode/opencode.jsonc"},
            {"gowails chatai", NULL},
            {"zshrc pre",      "~/.zshrc.pre-oh-my-zsh"},
            {"zshrc",          "~/.zshrc"},
            {"tmux",           "~/.tmux.conf"},
            {"ghossty",        "~/.config/ghostty/config"},
            {"zed",            "~/.config/zed/settings.json"},
        };
        int n = (int)(sizeof(seeds) / sizeof(seeds[0]));
        sqlite3_stmt *ins = NULL;
        if (sqlite3_prepare_v2(g_db,
                "INSERT INTO configs (name, path, sort_order, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?);",
                -1, &ins, NULL) == SQLITE_OK) {
            gint64 now = (gint64)time(NULL);
            for (int i = 0; i < n; i++) {
                char *path = seeds[i].path
                    ? g_strdup(seeds[i].path)
                    : get_dynamic_user_path();
                sqlite3_bind_text(ins, 1, seeds[i].name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 2, path,          -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (ins, 3, i);
                sqlite3_bind_int64(ins, 4, now);
                sqlite3_bind_int64(ins, 5, now);
                sqlite3_step(ins);
                sqlite3_reset(ins);
                g_free(path);
            }
            sqlite3_finalize(ins);
        }
    }
    return 0;
}

/* Returns a NULL-terminated GPtrArray* of Account*.  Caller frees with
 * g_ptr_array_free(arr, TRUE) — element free func handles Accounts. */
static GPtrArray *
db_list_accounts(void)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func((GDestroyNotify)account_free);

    const char *sql =
        "SELECT id, name, api_key, base_url, is_active FROM accounts "
        "ORDER BY is_active DESC, name COLLATE NOCASE ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[db] prepare list: %s\n", sqlite3_errmsg(g_db));
        return arr;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Account *a = g_new0(Account, 1);
        a->id        = sqlite3_column_int(stmt, 0);
        a->name      = g_strdup((const char *)sqlite3_column_text(stmt, 1));
        a->api_key   = g_strdup((const char *)sqlite3_column_text(stmt, 2));
        a->base_url  = g_strdup((const char *)sqlite3_column_text(stmt, 3));
        a->is_active = sqlite3_column_int(stmt, 4);
        g_ptr_array_add(arr, a);
    }
    sqlite3_finalize(stmt);
    return arr;
}

static int
db_insert(const char *name, const char *api_key, const char *base_url,
          char **err_out)
{
    const char *sql =
        "INSERT INTO accounts (name, api_key, base_url, is_active, "
        "created_at, updated_at) VALUES (?, ?, ?, 0, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    gint64 now = (gint64)time(NULL);
    sqlite3_bind_text(stmt, 1, name,     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, api_key,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, base_url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_bind_int64(stmt, 5, now);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int
db_update(int id, const char *name, const char *api_key, const char *base_url,
          char **err_out)
{
    const char *sql =
        "UPDATE accounts SET name=?, api_key=?, base_url=?, updated_at=? "
        "WHERE id=?;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    gint64 now = (gint64)time(NULL);
    sqlite3_bind_text(stmt, 1, name,     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, api_key,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, base_url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_bind_int(stmt, 5, id);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int
db_delete(int id, char **err_out)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "DELETE FROM accounts WHERE id=?;", -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

static int
db_set_active(int id, char **err_out)
{
    char *err = NULL;
    if (sqlite3_exec(g_db, "BEGIN;", NULL, NULL, &err) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(err ? err : "begin failed");
        sqlite3_free(err);
        return -1;
    }
    sqlite3_exec(g_db, "UPDATE accounts SET is_active=0;", NULL, NULL, NULL);

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(g_db,
        "UPDATE accounts SET is_active=1 WHERE id=?;", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
    return 0;
}

/* ── Configs ──────────────────────────────────────────────────────────── */

static void
config_entry_free(ConfigEntry *c)
{
    if (c == NULL) return;
    g_free(c->name);
    g_free(c->path);
    g_free(c);
}

static GPtrArray *
db_list_configs(void)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(
        (GDestroyNotify)config_entry_free);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "SELECT id, name, path FROM configs "
            "ORDER BY sort_order, id;",
            -1, &stmt, NULL) != SQLITE_OK)
        return arr;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ConfigEntry *c = g_new0(ConfigEntry, 1);
        c->id   = sqlite3_column_int(stmt, 0);
        c->name = g_strdup((const char *)sqlite3_column_text(stmt, 1));
        c->path = g_strdup((const char *)sqlite3_column_text(stmt, 2));
        g_ptr_array_add(arr, c);
    }
    sqlite3_finalize(stmt);
    return arr;
}

static int
db_insert_config(const char *name, const char *path, char **err_out)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO configs (name, path, sort_order, "
            "created_at, updated_at) VALUES (?, ?, "
            "(SELECT COALESCE(MAX(sort_order), -1) + 1 FROM configs), ?, ?);",
            -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    gint64 now = (gint64)time(NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);
    sqlite3_bind_int64(stmt, 4, now);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

static int
db_update_config(int id, const char *name, const char *path, char **err_out)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "UPDATE configs SET name=?, path=?, updated_at=? WHERE id=?;",
            -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, (gint64)time(NULL));
    sqlite3_bind_int(stmt, 4, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

static int
db_delete_config(int id, char **err_out)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "DELETE FROM configs WHERE id=?;", -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

/* ── Settings (key/value) ─────────────────────────────────────────────── */

/* Caller frees.  Returns NULL if not set. */
static char *
db_get_setting(const char *key)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "SELECT value FROM settings WHERE key=?;",
            -1, &stmt, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);

    char *val = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *t = (const char *)sqlite3_column_text(stmt, 0);
        if (t) val = g_strdup(t);
    }
    sqlite3_finalize(stmt);
    return val;
}

static void
db_set_setting(const char *key, const char *value)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO settings (key, value) VALUES (?, ?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
            -1, &stmt, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_text(stmt, 1, key,   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

/* Returns currently active account, or NULL. */
static Account *
db_get_active_account(void)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "SELECT id, name, api_key, base_url, is_active "
            "FROM accounts WHERE is_active=1 LIMIT 1;",
            -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    Account *a = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        a = g_new0(Account, 1);
        a->id        = sqlite3_column_int(stmt, 0);
        a->name      = g_strdup((const char *)sqlite3_column_text(stmt, 1));
        a->api_key   = g_strdup((const char *)sqlite3_column_text(stmt, 2));
        a->base_url  = g_strdup((const char *)sqlite3_column_text(stmt, 3));
        a->is_active = sqlite3_column_int(stmt, 4);
    }
    sqlite3_finalize(stmt);
    return a;
}

/* ── Plugins ──────────────────────────────────────────────────────────── */

static void
plugin_free(Plugin *p)
{
    if (p == NULL) return;
    g_free(p->name);
    g_free(p);
}

static GPtrArray *
db_list_plugins(void)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func((GDestroyNotify)plugin_free);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "SELECT name, enabled FROM plugins "
            "ORDER BY sort_order, name;",
            -1, &stmt, NULL) != SQLITE_OK)
        return arr;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Plugin *p = g_new0(Plugin, 1);
        p->name    = g_strdup((const char *)sqlite3_column_text(stmt, 0));
        p->enabled = sqlite3_column_int(stmt, 1);
        g_ptr_array_add(arr, p);
    }
    sqlite3_finalize(stmt);
    return arr;
}

static int
db_insert_plugin(const char *name, int enabled, char **err_out)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO plugins (name, enabled, sort_order) "
            "VALUES (?, ?, "
            "  (SELECT COALESCE(MAX(sort_order), -1) + 1 FROM plugins));",
            -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, enabled);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

static int
db_delete_plugin(const char *name, char **err_out)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "DELETE FROM plugins WHERE name=?;",
            -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

static int
db_set_plugin_enabled(const char *name, int enabled, char **err_out)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db,
            "UPDATE plugins SET enabled=? WHERE name=?;",
            -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, enabled);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

/* Render enabledPlugins as a JSON object body, properly indented to sit
 * under "  \"enabledPlugins\": " (4 leading spaces for inner keys).  Result
 * never contains a trailing newline. */
static char *
build_plugins_json_block(void)
{
    GPtrArray *plugins = db_list_plugins();
    if (plugins->len == 0) {
        g_ptr_array_free(plugins, TRUE);
        return g_strdup("{}");
    }

    GString *out = g_string_new("{\n");
    for (guint i = 0; i < plugins->len; i++) {
        Plugin *p   = g_ptr_array_index(plugins, i);
        char   *esc = json_escape(p->name);
        g_string_append_printf(out, "    \"%s\": %s%s\n",
            esc,
            p->enabled ? "true" : "false",
            (i + 1 < plugins->len) ? "," : "");
        g_free(esc);
    }
    g_string_append(out, "  }");

    g_ptr_array_free(plugins, TRUE);
    return g_string_free(out, FALSE);
}

/* ────────────────────────────────────────────────────────────────────────
 *  settings.json writer
 * ──────────────────────────────────────────────────────────────────────── */

/* Backup current settings.json -> settings.json.bak (overwrites previous). */
static int
backup_settings_json(char **err_out)
{
    char *src = get_claude_settings_path();
    char *dst = get_claude_settings_backup_path();

    int ret = 0;
    if (g_file_test(src, G_FILE_TEST_IS_REGULAR)) {
        gchar  *contents = NULL;
        gsize   length   = 0;
        GError *gerr     = NULL;
        if (!g_file_get_contents(src, &contents, &length, &gerr)) {
            if (err_out) *err_out = g_strdup(gerr->message);
            g_error_free(gerr);
            ret = -1;
            goto done;
        }
        if (!g_file_set_contents(dst, contents, length, &gerr)) {
            if (err_out) *err_out = g_strdup(gerr->message);
            g_error_free(gerr);
            ret = -1;
        }
        g_free(contents);
    }
done:
    g_free(src);
    g_free(dst);
    return ret;
}

static int
write_settings_json(const Account *a, char **err_out)
{
    const char *home = g_get_home_dir();
    if (home == NULL) {
        if (err_out) *err_out = g_strdup("no home directory");
        return -1;
    }

    /* ensure ~/.claude exists */
    char *claude_dir = g_strdup_printf("%s/.claude", home);
    ensure_dir(claude_dir);
    g_free(claude_dir);

    char *path = get_claude_settings_path();
    char *json = build_settings_json(a);

    GError *gerr = NULL;
    int ret = 0;
    if (!g_file_set_contents(path, json, -1, &gerr)) {
        if (err_out) *err_out = g_strdup(gerr->message);
        g_error_free(gerr);
        ret = -1;
    } else {
        /* chmod 600 to protect API key */
        chmod(path, 0600);
    }
    g_free(json);
    g_free(path);
    return ret;
}

static int
write_reset_settings_json(char **err_out)
{
    const char *home = g_get_home_dir();
    if (home == NULL) {
        if (err_out) *err_out = g_strdup("no home directory");
        return -1;
    }
    char *claude_dir = g_strdup_printf("%s/.claude", home);
    ensure_dir(claude_dir);
    g_free(claude_dir);

    char *path = get_claude_settings_path();
    char *json = build_reset_settings_json();
    GError *gerr = NULL;
    int ret = 0;
    if (!g_file_set_contents(path, json, -1, &gerr)) {
        if (err_out) *err_out = g_strdup(gerr->message);
        g_error_free(gerr);
        ret = -1;
    } else {
        chmod(path, 0600);
    }
    g_free(json);
    g_free(path);
    return ret;
}

/* Write a per-account preview JSON file and return the path the caller
 * should open with subl.  Caller frees. */
static char *
write_preview_json(const Account *a)
{
    char *dir = get_app_data_dir();
    char *preview_dir = g_strdup_printf("%s/preview", dir);
    ensure_dir(preview_dir);
    g_free(dir);

    /* sanitize name */
    GString *safe = g_string_new(NULL);
    for (const char *p = a->name; *p; p++) {
        if (g_ascii_isalnum(*p) || *p == '-' || *p == '_')
            g_string_append_c(safe, *p);
        else
            g_string_append_c(safe, '_');
    }
    char *path = g_strdup_printf("%s/%s.json", preview_dir, safe->str);
    g_string_free(safe, TRUE);
    g_free(preview_dir);

    char *json = build_settings_json(a);
    g_file_set_contents(path, json, -1, NULL);
    g_free(json);
    return path;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Mask helper
 * ──────────────────────────────────────────────────────────────────────── */

static char *
mask_api_key(const char *key)
{
    if (key == NULL) return g_strdup("");
    size_t len = strlen(key);
    if (len <= 12) return g_strdup("••••••••");
    char head[9];  /* up to 8 chars */
    char tail[5];  /* up to 4 chars */
    size_t hn = 8, tn = 4;
    memcpy(head, key, hn); head[hn] = 0;
    memcpy(tail, key + len - tn, tn); tail[tn] = 0;
    return g_strdup_printf("%s•••••%s", head, tail);
}

/* ────────────────────────────────────────────────────────────────────────
 *  Home tab -- dashboard with shortcut cards + keyboard shortcuts
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char *icon;
    const char *title;
    const char *desc;
    int         tab_index;
} HomeCard;

static const HomeCard g_home_cards[] = {
    {"📄", "OpenCode",        "Resume dev sessions",         1},
    {"🤖", "Claude",          "Indexed CLI sessions",        2},
    {"🔑", "SSH",             "Remote connections",          3},
    {"💻", "Terminal",        "Local PTY terminal",          4},
    {"🌐", "Chat Web",        "Embedded AI chats",           5},
    {"🔥", "Kimchi",          "Kimchi session index",        6},
    {"📱", "Mimo",            "Mimo session index",          7},
    {"⚡", "Freebuff",        "Freebuff session index",      8},
    {"🔌", "Port Monitor",    "Active TCP ports",            9},
    {"⚙️", "Config Opener",   "Edit config files",          10},
    {"🔄", "Claude Switcher", "Switch API accounts",        11},
    {NULL, NULL, NULL, -1},
};

static void
on_home_card_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    int idx = GPOINTER_TO_INT(user_data);
    if (g_notebook != NULL && idx >= 0) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), idx);
    }
}

static GtkWidget *
create_home_card(const HomeCard *card)
{
    GtkWidget *btn = gtk_button_new();
    gtk_style_context_add_class(
        gtk_widget_get_style_context(btn), "home-card");
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    g_signal_connect(btn, "clicked",
                     G_CALLBACK(on_home_card_clicked),
                     GINT_TO_POINTER(card->tab_index));

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_halign(vbox, GTK_ALIGN_START);

    GtkWidget *icon = gtk_label_new(card->icon);
    gtk_widget_set_halign(icon, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(icon), "home-card-icon");
    gtk_box_pack_start(GTK_BOX(vbox), icon, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new(card->title);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(title), "home-card-title");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *desc = gtk_label_new(card->desc);
    gtk_widget_set_halign(desc, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(desc), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(desc), "home-card-desc");
    gtk_box_pack_start(GTK_BOX(vbox), desc, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(btn), vbox);
    return btn;
}

static GtkWidget *
create_shortcut_row(const char *key, const char *desc)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(hbox, 2);
    gtk_widget_set_margin_bottom(hbox, 2);

    GtkWidget *key_label = gtk_label_new(key);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(key_label), "home-shortcut-key");
    gtk_widget_set_halign(key_label, GTK_ALIGN_START);
    gtk_widget_set_size_request(key_label, 100, -1);
    gtk_box_pack_start(GTK_BOX(hbox), key_label, FALSE, FALSE, 0);

    GtkWidget *desc_label = gtk_label_new(desc);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(desc_label), "home-shortcut-desc");
    gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(desc_label, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), desc_label, TRUE, TRUE, 0);

    return hbox;
}

static GtkWidget *
build_home_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(content,   24);
    gtk_widget_set_margin_end(content,     24);
    gtk_widget_set_margin_top(content,     20);
    gtk_widget_set_margin_bottom(content,  20);

    GtkWidget *welcome = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(welcome),
        "<span size=\"xx-large\" font_weight=\"bold\">"
        "Opentool Desktop</span>");
    gtk_widget_set_halign(welcome, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(welcome, 4);
    gtk_box_pack_start(GTK_BOX(content), welcome, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new(
        "Manage CLI sessions, terminals, and developer tools from one place.");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(subtitle), "subtitle-label");
    gtk_widget_set_margin_bottom(subtitle, 16);
    gtk_box_pack_start(GTK_BOX(content), subtitle, FALSE, FALSE, 0);

    GtkWidget *sec_title = gtk_label_new("Quick Access");
    gtk_widget_set_halign(sec_title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(sec_title), "home-section-title");
    gtk_box_pack_start(GTK_BOX(content), sec_title, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    int col = 0;
    int row = 0;
    const int COLS = 4;
    for (int i = 0; g_home_cards[i].icon != NULL; i++) {
        GtkWidget *card = create_home_card(&g_home_cards[i]);
        gtk_widget_set_hexpand(card, TRUE);
        gtk_widget_set_vexpand(card, TRUE);
        gtk_grid_attach(GTK_GRID(grid), card, col, row, 1, 1);
        col++;
        if (col >= COLS) {
            col = 0;
            row++;
        }
    }
    gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);

    GtkWidget *shortcuts_title = gtk_label_new("Keyboard Shortcuts");
    gtk_widget_set_halign(shortcuts_title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(shortcuts_title), "home-section-title");
    gtk_widget_set_margin_top(shortcuts_title, 16);
    gtk_box_pack_start(GTK_BOX(content), shortcuts_title, FALSE, FALSE, 0);

    static const struct {
        const char *key;
        const char *desc;
    } shortcuts[] = {
        {"Ctrl+K",     "Go to Home"},
        {"Ctrl+P",     "Command Palette"},
        {"Ctrl+W",     "Close window"},
        {"Ctrl+Q",     "Quit application"},
        {"Shift+↑/↓",  "Scroll terminal (3 lines)"},
        {"Shift+Enter","Insert newline without submitting"},
        {NULL, NULL},
    };

    GtkWidget *shortcuts_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    for (int i = 0; shortcuts[i].key != NULL; i++) {
        GtkWidget *srow = create_shortcut_row(shortcuts[i].key,
                                               shortcuts[i].desc);
        gtk_box_pack_start(GTK_BOX(shortcuts_box), srow, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(content), shortcuts_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(scrolled), content);
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  OpenCode Session Manager tab
 *  Reads ~/.local/share/opencode/opencode.db (external SQLite, read-only)
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    char *id;
    char *title;
    char *directory;
    long  time_created;
    long  time_updated;
    char *project_id;
} OpenCodeSession;

static GtkWidget *g_opencode_list_box  = NULL;
static GtkWidget *g_opencode_count_lbl = NULL;
static GtkWidget *g_opencode_search    = NULL;

static void refresh_opencode_list(void);

static void
opencode_session_free(OpenCodeSession *s)
{
    if (s == NULL) return;
    g_free(s->id);
    g_free(s->title);
    g_free(s->directory);
    g_free(s->project_id);
    g_free(s);
}

static char *
get_opencode_db_path(void)
{
    const char *xdg = g_getenv("XDG_DATA_HOME");
    if (xdg != NULL && *xdg != '\0')
        return g_strdup_printf("%s/opencode/opencode.db", xdg);
    const char *home = g_get_home_dir();
    if (home == NULL) home = "/tmp";
    return g_strdup_printf("%s/.local/share/opencode/opencode.db", home);
}

static char *
format_relative_time(long msecs)
{
    time_t now = time(NULL);
    long secs_ago = now - (msecs / 1000);
    if (secs_ago < 0) secs_ago = 0;

    if (secs_ago < 60)
        return g_strdup("just now");
    else if (secs_ago < 3600)
        return g_strdup_printf("%ldm ago", secs_ago / 60);
    else if (secs_ago < 86400)
        return g_strdup_printf("%ldh ago", secs_ago / 3600);
    else if (secs_ago < 2592000)
        return g_strdup_printf("%ldd ago", secs_ago / 86400);
    else
        return g_strdup_printf("%ldmo ago", secs_ago / 2592000);
}

static char *
display_path(const char *path)
{
    if (path == NULL) return g_strdup("");
    const char *home = g_get_home_dir();
    if (home != NULL && g_str_has_prefix(path, home))
        return g_strdup_printf("~%s", path + strlen(home));
    return g_strdup(path);
}

static GPtrArray *
opencode_load_sessions(const char *search)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(
        (GDestroyNotify)opencode_session_free);

    char *db_path = get_opencode_db_path();
    if (!g_file_test(db_path, G_FILE_TEST_IS_REGULAR)) {
        g_free(db_path);
        return arr;
    }

    sqlite3 *oc_db = NULL;
    if (sqlite3_open_v2(db_path, &oc_db,
                        SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        g_free(db_path);
        if (oc_db) sqlite3_close(oc_db);
        return arr;
    }

    const char *base_sql =
        "SELECT id, title, directory, time_created, time_updated, "
        "project_id FROM session WHERE parent_id IS NULL";

    char sql[2048];
    if (search != NULL && *search != '\0') {
        char *esc = g_strescape(search, NULL);
        snprintf(sql, sizeof(sql),
            "%s AND (title LIKE '%%%s%%' OR directory LIKE '%%%s%%') "
            "ORDER BY time_updated DESC LIMIT 500;",
            base_sql, esc, esc);
        g_free(esc);
    } else {
        snprintf(sql, sizeof(sql),
            "%s ORDER BY time_updated DESC LIMIT 500;", base_sql);
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(oc_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            OpenCodeSession *s = g_new0(OpenCodeSession, 1);
            s->id           = g_strdup((const char *)sqlite3_column_text(stmt, 0));
            s->title        = g_strdup((const char *)sqlite3_column_text(stmt, 1));
            s->directory    = g_strdup((const char *)sqlite3_column_text(stmt, 2));
            s->time_created = sqlite3_column_int64(stmt, 3);
            s->time_updated = sqlite3_column_int64(stmt, 4);
            s->project_id   = g_strdup((const char *)sqlite3_column_text(stmt, 5));
            if (s->title == NULL || *s->title == '\0') s->title = g_strdup("(untitled)");
            g_ptr_array_add(arr, s);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(oc_db);
    g_free(db_path);
    return arr;
}

static void
on_opencode_resume_clicked(GtkButton *button, gpointer user_data)
{
    OpenCodeSession *s = (OpenCodeSession *)user_data;
    (void)button;
    char *cmd = g_strdup_printf("opencode -s %s", s->id);
    pid_t pid = fork();
    if (pid == 0) {
        execlp("opencode", "opencode", "-s", s->id, (char *)NULL);
        _exit(1);
    }
    g_free(cmd);
    if (g_status_label)
        set_status("Launching opencode session: %s", s->title);
}

static void
on_opencode_open_dir_clicked(GtkButton *button, gpointer user_data)
{
    OpenCodeSession *s = (OpenCodeSession *)user_data;
    if (s->directory == NULL) return;
    launch_editor(GTK_WIDGET(button), s->directory);
}

static void
on_opencode_delete_clicked(GtkButton *button, gpointer user_data)
{
    OpenCodeSession *s = (OpenCodeSession *)user_data;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Delete session '%s'?", s->title);
    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp != GTK_RESPONSE_YES) return;

    char *db_path = get_opencode_db_path();
    sqlite3 *oc_db = NULL;
    if (sqlite3_open(db_path, &oc_db) == SQLITE_OK) {
        char *sql = g_strdup_printf(
            "DELETE FROM session WHERE id = '%s';", s->id);
        sqlite3_exec(oc_db, sql, NULL, NULL, NULL);
        g_free(sql);
        sqlite3_close(oc_db);
        set_status("Deleted session '%s'", s->title);
        refresh_opencode_list();
    }
    g_free(db_path);
}

static void refresh_opencode_list(void);

static GtkWidget *
create_opencode_row(OpenCodeSession *s)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox,   12);
    gtk_widget_set_margin_end(hbox,     8);
    gtk_widget_set_margin_top(hbox,     6);
    gtk_widget_set_margin_bottom(hbox,  6);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);

    GString *markup = g_string_new(NULL);
    g_string_printf(markup, "<b>%s</b>", s->title);
    char *rel = format_relative_time(s->time_updated);
    g_string_append_printf(markup, "  <span size=\"small\" "
        "foreground=\"#86868b\">%s</span>", rel);
    g_free(rel);

    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_lbl), markup->str);
    g_string_free(markup, TRUE);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

    char *dpath = display_path(s->directory);
    GtkWidget *dir_lbl = gtk_label_new(dpath);
    g_free(dpath);
    gtk_widget_set_halign(dir_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(dir_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(dir_lbl), "account-meta");
    gtk_box_pack_start(GTK_BOX(vbox), dir_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    GtkWidget *resume_btn = gtk_button_new_with_label("Resume");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(resume_btn), "open-button");
    g_signal_connect(resume_btn, "clicked",
                     G_CALLBACK(on_opencode_resume_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), resume_btn, FALSE, FALSE, 0);

    GtkWidget *open_dir_btn = gtk_button_new_with_label("Open Dir");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(open_dir_btn), "edit-button");
    g_signal_connect(open_dir_btn, "clicked",
                     G_CALLBACK(on_opencode_open_dir_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), open_dir_btn, FALSE, FALSE, 0);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(del_btn), "delete-button");
    g_signal_connect(del_btn, "clicked",
                     G_CALLBACK(on_opencode_delete_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), del_btn, FALSE, FALSE, 0);

    return hbox;
}

static void
on_opencode_search_changed(GtkSearchEntry *entry, gpointer data)
{
    (void)data;
    (void)entry;
    refresh_opencode_list();
}

static void
refresh_opencode_list(void)
{
    if (g_opencode_list_box == NULL) return;

    GList *kids = gtk_container_get_children(
        GTK_CONTAINER(g_opencode_list_box));
    for (GList *l = kids; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    const char *search = g_opencode_search
        ? gtk_entry_get_text(GTK_ENTRY(g_opencode_search))
        : NULL;

    GPtrArray *sessions = opencode_load_sessions(search);

    if (g_opencode_count_lbl) {
        char *count_text = g_strdup_printf("%d sessions", sessions->len);
        gtk_label_set_text(GTK_LABEL(g_opencode_count_lbl), count_text);
        g_free(count_text);
    }

    if (sessions->len == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        char *db_path = get_opencode_db_path();
        char *msg;
        if (!g_file_test(db_path, G_FILE_TEST_IS_REGULAR))
            msg = g_strdup_printf(
                "opencode.db not found at:\n%s\n\n"
                "Start an opencode session first to populate the index.",
                db_path);
        else
            msg = g_strdup("No sessions found.");
        g_free(db_path);

        GtkWidget *empty = gtk_label_new(msg);
        g_free(msg);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(empty), "empty-label");
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(row), empty);
        gtk_container_add(GTK_CONTAINER(g_opencode_list_box), row);
    } else {
        for (guint i = 0; i < sessions->len; i++) {
            OpenCodeSession *s = g_ptr_array_index(sessions, i);
            g_ptr_array_index(sessions, i) = NULL;

            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
            GtkWidget *content = create_opencode_row(s);
            gtk_container_add(GTK_CONTAINER(row), content);
            gtk_container_add(GTK_CONTAINER(g_opencode_list_box), row);

            g_object_set_data_full(G_OBJECT(row), "session",
                s, (GDestroyNotify)opencode_session_free);
        }
    }
    g_ptr_array_free(sessions, TRUE);
    gtk_widget_show_all(g_opencode_list_box);
}

static GtkWidget *
build_opencode_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(header), "header-box");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(header_text, TRUE);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span font_weight=\"bold\" size=\"12000\">"
        "OpenCode Sessions</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(title), "title-label");
    gtk_box_pack_start(GTK_BOX(header_text), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new(
        "Resume or manage OpenCode CLI sessions");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(subtitle), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(header_text), subtitle, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), header_text, TRUE, TRUE, 0);

    g_opencode_count_lbl = gtk_label_new("");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(g_opencode_count_lbl),
        "subtitle-label");
    gtk_box_pack_end(GTK_BOX(header), g_opencode_count_lbl, FALSE, FALSE, 0);

    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(refresh_btn), "edit-button");
    g_signal_connect_swapped(refresh_btn, "clicked",
        G_CALLBACK(refresh_opencode_list), NULL);
    gtk_box_pack_end(GTK_BOX(header), refresh_btn, FALSE, FALSE, 0);

    /* Search bar */
    GtkWidget *search_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(search_bar,   12);
    gtk_widget_set_margin_end(search_bar,     12);
    gtk_widget_set_margin_top(search_bar,      8);
    gtk_widget_set_margin_bottom(search_bar,   8);

    g_opencode_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_opencode_search),
        "Search by title or directory...");
    gtk_widget_set_hexpand(g_opencode_search, TRUE);
    g_signal_connect(g_opencode_search, "search-changed",
                     G_CALLBACK(on_opencode_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(search_bar), g_opencode_search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), search_bar, FALSE, FALSE, 0);

    /* Scrolled list */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    g_opencode_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_opencode_list_box),
                                     GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), g_opencode_list_box);

    refresh_opencode_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Claude Session Manager tab
 *  Indexes ~/.claude/projects/<dir>/<id>.jsonl sessions
 *  SQLite cache in claude_index.db, inotify watcher for live updates
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    char *session_id;
    char *project_dir_name;
    char *decoded_path;
    char *ai_title;
    char *first_message;
    long  last_modified;
} ClaudeSession;

static GtkWidget *g_claude_list_box  = NULL;
static GtkWidget *g_claude_count_lbl = NULL;
static GtkWidget *g_claude_search    = NULL;
static sqlite3   *g_claude_idx_db    = NULL;

static void refresh_claude_list(void);

static void
claude_session_free(ClaudeSession *s)
{
    if (s == NULL) return;
    g_free(s->session_id);
    g_free(s->project_dir_name);
    g_free(s->decoded_path);
    g_free(s->ai_title);
    g_free(s->first_message);
    g_free(s);
}

static char *
get_claude_projects_dir(void)
{
    const char *home = g_get_home_dir();
    if (home == NULL) home = "/tmp";
    return g_strdup_printf("%s/.claude/projects", home);
}

static char *
get_claude_index_db_path(void)
{
    char *dir = get_app_data_dir();
    char *path = g_strdup_printf("%s/claude_index.db", dir);
    g_free(dir);
    return path;
}

/* Claude encodes paths as: /Users/fajar/proj -> -Users-fajar-proj
 * Decoding: split on '-', then use backtracking to find real directories */
static char *
claude_decode_path(const char *encoded)
{
    if (encoded == NULL || *encoded == '\0') return g_strdup("");

    const char *start = encoded;
    if (*start == '-') start++;
    if (*start == '\0') return g_strdup("/");

    gchar **parts = g_strsplit(start, "-", -1);
    int nparts = 0;
    while (parts[nparts] != NULL) nparts++;

    if (nparts == 0) {
        g_strfreev(parts);
        return g_strdup("/");
    }

    GString *result = g_string_new(NULL);
    int i = 0;

    while (i < nparts) {
        gboolean found = FALSE;

        for (int len = nparts - i; len >= 1; len--) {
            GString *candidate = g_string_new("/");
            if (result->len > 1)
                g_string_append_len(candidate, result->str + 1, result->len - 1);
            else
                g_string_truncate(candidate, 0);

            for (int j = 0; j < len; j++) {
                g_string_append_c(candidate, '/');
                g_string_append(candidate, parts[i + j]);
            }

            gboolean is_dir = g_file_test(candidate->str,
                                           G_FILE_TEST_IS_DIR);
            if (is_dir || (i + len == nparts)) {
                g_string_assign(result, candidate->str);
                g_string_free(candidate, TRUE);
                i += len;
                found = TRUE;
                break;
            }
            g_string_free(candidate, TRUE);
        }

        if (!found) {
            g_string_append_c(result, '/');
            g_string_append(result, parts[i]);
            i++;
        }
    }

    char *final = g_string_free(result, FALSE);
    g_strfreev(parts);
    return final;
}

/* Simple JSONL line-by-line parser for Claude session files.
 * Extracts ai_title (type=="ai-title") and first user message. */
static void
claude_parse_jsonl(const char *filepath, char **out_title, char **out_msg)
{
    *out_title = NULL;
    *out_msg   = NULL;

    FILE *f = fopen(filepath, "r");
    if (f == NULL) return;

    char line[16384];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '{') continue;

        gboolean has_ai_title = (strstr(p, "\"type\"") != NULL &&
                                 strstr(p, "\"ai-title\"") != NULL) ||
                                (strstr(p, "\"aiTitle\"") != NULL);

        if (has_ai_title && *out_title == NULL) {
            char *at = strstr(p, "\"aiTitle\"");
            if (at == NULL) at = strstr(p, "\"aiTitle\":");
            if (at != NULL) {
                char *colon = strchr(at, ':');
                if (colon != NULL) {
                    char *start = colon + 1;
                    while (*start == ' ') start++;
                    if (*start == '"') {
                        start++;
                        char *end = start;
                        while (*end && *end != '"' && (end[-1] != '\\')) end++;
                        *out_title = g_strndup(start, end - start);
                    }
                }
            }
        }

        gboolean has_user_msg = strstr(p, "\"type\"") != NULL &&
                                strstr(p, "\"user\"") != NULL;
        if (strcmp(strstr(p, "\"type\"") ? strstr(p, "\"type\"") : "", "") != 0) {
            char *type_str = strstr(p, "\"type\"");
            if (type_str) {
                char *colon2 = strchr(type_str, ':');
                if (colon2) {
                    char *val = colon2 + 1;
                    while (*val == ' ') val++;
                    if (*val == '"') {
                        val++;
                        if (strncmp(val, "user\"", 5) == 0) {
                            has_user_msg = TRUE;
                        } else {
                            has_user_msg = FALSE;
                        }
                    }
                }
            }
        }

        if (has_user_msg && *out_msg == NULL) {
            char *content = strstr(p, "\"content\"");
            if (content != NULL) {
                char *colon = strchr(content, ':');
                if (colon != NULL) {
                    char *val = colon + 1;
                    while (*val == ' ') val++;
                    if (*val == '"') {
                        val++;
                        char *end = val;
                        while (*end && *end != '"') {
                            if (*end == '\\' && end[1]) end++;
                            end++;
                        }
                        *out_msg = g_strndup(val,
                            MIN((gsize)(end - val), 150));
                    }
                }
            }
        }

        if (*out_title != NULL && *out_msg != NULL) break;
    }
    fclose(f);
}

/* Initialize the Claude session index database */
static int
claude_index_db_init(void)
{
    char *dir = get_app_data_dir();
    ensure_dir(dir);
    g_free(dir);

    char *path = get_claude_index_db_path();
    int rc = sqlite3_open(path, &g_claude_idx_db);
    g_free(path);
    if (rc != SQLITE_OK) {
        if (g_claude_idx_db) { sqlite3_close(g_claude_idx_db); g_claude_idx_db = NULL; }
        return -1;
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS claude_sessions ("
        "  session_id TEXT PRIMARY KEY,"
        "  project_dir TEXT NOT NULL,"
        "  decoded_path TEXT NOT NULL,"
        "  ai_title TEXT,"
        "  first_message TEXT,"
        "  last_modified INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_claude_project "
        "  ON claude_sessions(project_dir);";
    sqlite3_exec(g_claude_idx_db, schema, NULL, NULL, NULL);
    return 0;
}

static void
claude_index_upsert(ClaudeSession *s)
{
    if (g_claude_idx_db == NULL) return;
    const char *sql =
        "INSERT OR REPLACE INTO claude_sessions "
        "(session_id, project_dir, decoded_path, ai_title, first_message, "
        " last_modified) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_claude_idx_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_text(stmt, 1, s->session_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, s->project_dir_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, s->decoded_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, s->ai_title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, s->first_message, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, s->last_modified);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void
claude_index_remove(const char *session_id)
{
    if (g_claude_idx_db == NULL) return;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_claude_idx_db,
            "DELETE FROM claude_sessions WHERE session_id=?;",
            -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void
claude_index_file(const char *filepath)
{
    char *proj_dir_name = NULL;
    char *session_id    = NULL;

    char *basename = g_path_get_basename(filepath);
    if (basename == NULL) return;
    char *dot = strrchr(basename, '.');
    if (dot) *dot = '\0';
    session_id = g_strdup(basename);
    g_free(basename);

    char *dir = g_path_get_dirname(filepath);
    proj_dir_name = g_path_get_basename(dir);
    g_free(dir);

    if (proj_dir_name == NULL || session_id == NULL) {
        g_free(proj_dir_name);
        g_free(session_id);
        return;
    }

    gchar *data = NULL;
    gsize len = 0;
    if (!g_file_get_contents(filepath, &data, &len, NULL)) {
        g_free(proj_dir_name);
        g_free(session_id);
        return;
    }
    g_free(data);

    GStatBuf st;
    if (g_stat(filepath, &st) != 0) {
        g_free(proj_dir_name);
        g_free(session_id);
        return;
    }

    char *title = NULL, *msg = NULL;
    claude_parse_jsonl(filepath, &title, &msg);

    char *decoded = claude_decode_path(proj_dir_name);

    ClaudeSession s = {
        .session_id       = session_id,
        .project_dir_name = proj_dir_name,
        .decoded_path     = decoded,
        .ai_title         = title,
        .first_message    = msg,
        .last_modified    = (long)st.st_mtime,
    };
    claude_index_upsert(&s);

    g_free(decoded);
    g_free(title);
    g_free(msg);
    g_free(proj_dir_name);
    g_free(session_id);
}

/* Full scan of ~/.claude/projects/ */
static void
claude_scan_all_projects(void)
{
    if (g_claude_idx_db == NULL) return;

    char *base = get_claude_projects_dir();
    DIR *d = opendir(base);
    if (d == NULL) { g_free(base); return; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char *subdir = g_build_filename(base, ent->d_name, NULL);
        if (!g_file_test(subdir, G_FILE_TEST_IS_DIR)) {
            g_free(subdir);
            continue;
        }

        DIR *sd = opendir(subdir);
        if (sd != NULL) {
            struct dirent *sent;
            while ((sent = readdir(sd)) != NULL) {
                char *name = sent->d_name;
                int len = strlen(name);
                if (len < 7) continue;
                if (strcmp(name + len - 6, ".jsonl") != 0) continue;
                char *jp = g_build_filename(subdir, name, NULL);

                if (strstr(jp, "/subagents/") != NULL) {
                    g_free(jp);
                    continue;
                }
                claude_index_file(jp);
                g_free(jp);
            }
            closedir(sd);
        }
        g_free(subdir);
    }
    closedir(d);
    g_free(base);
}

static GPtrArray *
claude_load_sessions(const char *search)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(
        (GDestroyNotify)claude_session_free);
    if (g_claude_idx_db == NULL) return arr;

    const char *base_sql =
        "SELECT session_id, project_dir, decoded_path, ai_title, "
        "first_message, last_modified FROM claude_sessions";

    char sql[2048];
    if (search != NULL && *search != '\0') {
        char *esc = g_strescape(search, NULL);
        snprintf(sql, sizeof(sql),
            "%s WHERE ai_title LIKE '%%%s%%' OR decoded_path LIKE '%%%s%%' "
            "OR first_message LIKE '%%%s%%' "
            "ORDER BY last_modified DESC LIMIT 500;", base_sql, esc, esc, esc);
        g_free(esc);
    } else {
        snprintf(sql, sizeof(sql),
            "%s ORDER BY last_modified DESC LIMIT 500;", base_sql);
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_claude_idx_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ClaudeSession *s = g_new0(ClaudeSession, 1);
            s->session_id       = g_strdup((const char *)sqlite3_column_text(stmt, 0));
            s->project_dir_name = g_strdup((const char *)sqlite3_column_text(stmt, 1));
            s->decoded_path     = g_strdup((const char *)sqlite3_column_text(stmt, 2));
            s->ai_title         = g_strdup((const char *)sqlite3_column_text(stmt, 3));
            s->first_message    = g_strdup((const char *)sqlite3_column_text(stmt, 4));
            s->last_modified    = sqlite3_column_int64(stmt, 5);
            g_ptr_array_add(arr, s);
        }
    }
    sqlite3_finalize(stmt);
    return arr;
}

static const char *
claude_display_title(ClaudeSession *s)
{
    if (s->ai_title && *s->ai_title) return s->ai_title;
    if (s->first_message && *s->first_message) return s->first_message;
    return "Claude Session";
}

static void
on_claude_resume_clicked(GtkButton *button, gpointer user_data)
{
    ClaudeSession *s = (ClaudeSession *)user_data;
    (void)button;
    pid_t pid = fork();
    if (pid == 0) {
        execlp("claude", "claude", "--resume", s->session_id, (char *)NULL);
        _exit(1);
    }
    set_status("Launching claude session: %s", claude_display_title(s));
}

static void
on_claude_open_dir_clicked(GtkButton *button, gpointer user_data)
{
    ClaudeSession *s = (ClaudeSession *)user_data;
    if (s->decoded_path) launch_editor(GTK_WIDGET(button), s->decoded_path);
}

static void
on_claude_delete_clicked(GtkButton *button, gpointer user_data)
{
    ClaudeSession *s = (ClaudeSession *)user_data;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Delete session '%s'?", claude_display_title(s));
    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp != GTK_RESPONSE_YES) return;

    char *proj_dir = get_claude_projects_dir();
    char *fp = g_strdup_printf("%s/%s/%s.jsonl",
        proj_dir, s->project_dir_name, s->session_id);
    g_unlink(fp);
    g_free(fp);
    g_free(proj_dir);

    claude_index_remove(s->session_id);
    set_status("Deleted Claude session '%s'", claude_display_title(s));
    refresh_claude_list();
}

static GtkWidget *
create_claude_row(ClaudeSession *s)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox,   12);
    gtk_widget_set_margin_end(hbox,     8);
    gtk_widget_set_margin_top(hbox,     6);
    gtk_widget_set_margin_bottom(hbox,  6);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);

    GtkWidget *title_lbl = gtk_label_new(claude_display_title(s));
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(title_lbl), "account-name");
    gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

    GtkWidget *meta = gtk_label_new(NULL);
    char *dpath = display_path(s->decoded_path);
    char *rel = format_relative_time((long)s->last_modified * 1000);
    char *markup = g_strdup_printf(
        "<span size=\"small\" foreground=\"#86868b\">%s · %s</span>",
        dpath, rel);
    gtk_label_set_markup(GTK_LABEL(meta), markup);
    g_free(markup); g_free(dpath); g_free(rel);
    gtk_widget_set_halign(meta, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(meta), PANGO_ELLIPSIZE_MIDDLE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(meta), "account-meta");
    gtk_box_pack_start(GTK_BOX(vbox), meta, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    GtkWidget *resume_btn = gtk_button_new_with_label("Resume");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(resume_btn), "open-button");
    g_signal_connect(resume_btn, "clicked",
        G_CALLBACK(on_claude_resume_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), resume_btn, FALSE, FALSE, 0);

    GtkWidget *open_dir_btn = gtk_button_new_with_label("Open Dir");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(open_dir_btn), "edit-button");
    g_signal_connect(open_dir_btn, "clicked",
        G_CALLBACK(on_claude_open_dir_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), open_dir_btn, FALSE, FALSE, 0);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(del_btn), "delete-button");
    g_signal_connect(del_btn, "clicked",
        G_CALLBACK(on_claude_delete_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), del_btn, FALSE, FALSE, 0);

    return hbox;
}

static void
on_claude_search_changed(GtkSearchEntry *entry, gpointer data)
{
    (void)entry; (void)data;
    refresh_claude_list();
}

static void
refresh_claude_list(void)
{
    if (g_claude_list_box == NULL) return;

    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_claude_list_box));
    for (GList *l = kids; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    const char *search = g_claude_search
        ? gtk_entry_get_text(GTK_ENTRY(g_claude_search)) : NULL;
    GPtrArray *sessions = claude_load_sessions(search);

    if (g_claude_count_lbl) {
        char *count_text = g_strdup_printf("%d sessions", sessions->len);
        gtk_label_set_text(GTK_LABEL(g_claude_count_lbl), count_text);
        g_free(count_text);
    }

    if (sessions->len == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        GtkWidget *empty = gtk_label_new("No Claude sessions indexed.\n"
            "Start a Claude session to populate the index.");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(empty), "empty-label");
        gtk_container_add(GTK_CONTAINER(row), empty);
        gtk_container_add(GTK_CONTAINER(g_claude_list_box), row);
    } else {
        for (guint i = 0; i < sessions->len; i++) {
            ClaudeSession *s = g_ptr_array_index(sessions, i);
            g_ptr_array_index(sessions, i) = NULL;
            GtkWidget *r = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r), FALSE);
            gtk_container_add(GTK_CONTAINER(r), create_claude_row(s));
            gtk_container_add(GTK_CONTAINER(g_claude_list_box), r);
            g_object_set_data_full(G_OBJECT(r), "session",
                s, (GDestroyNotify)claude_session_free);
        }
    }
    g_ptr_array_free(sessions, TRUE);
    gtk_widget_show_all(g_claude_list_box);
}

static void
on_claude_rescan_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    claude_scan_all_projects();
    refresh_claude_list();
    set_status("✓ Re-scanned ~/.claude/projects/");
}

static GtkWidget *
build_claude_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(header), "header-box");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(header_text, TRUE);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span font_weight=\"bold\" size=\"12000\">"
        "Claude Sessions</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(title), "title-label");
    gtk_box_pack_start(GTK_BOX(header_text), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new(
        "Indexed CLI sessions from ~/.claude/projects/");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(subtitle), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(header_text), subtitle, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), header_text, TRUE, TRUE, 0);

    g_claude_count_lbl = gtk_label_new("");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(g_claude_count_lbl), "subtitle-label");
    gtk_box_pack_end(GTK_BOX(header), g_claude_count_lbl, FALSE, FALSE, 0);

    GtkWidget *rescan_btn = gtk_button_new_with_label("Rescan");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(rescan_btn), "edit-button");
    g_signal_connect(rescan_btn, "clicked",
        G_CALLBACK(on_claude_rescan_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(header), rescan_btn, FALSE, FALSE, 0);

    /* Search */
    GtkWidget *search_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(search_bar,   12);
    gtk_widget_set_margin_end(search_bar,     12);
    gtk_widget_set_margin_top(search_bar,      8);
    gtk_widget_set_margin_bottom(search_bar,   8);
    g_claude_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_claude_search),
        "Search by title, message, or directory...");
    gtk_widget_set_hexpand(g_claude_search, TRUE);
    g_signal_connect(g_claude_search, "search-changed",
        G_CALLBACK(on_claude_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(search_bar), g_claude_search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), search_bar, FALSE, FALSE, 0);

    /* Scrolled list */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    g_claude_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_claude_list_box),
                                     GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), g_claude_list_box);

    claude_index_db_init();
    claude_scan_all_projects();
    refresh_claude_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  SSH Connection Manager tab
 *  CRUD for SSH connections, import from ~/.ssh/config, grouped display
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    char *id;
    char *name;
    char *host;
    int   port;
    char *user;
    char *auth_type;
    char *credential;
    char *group_name;
    long  created_at;
    long  updated_at;
} SSHConn;

static sqlite3   *g_ssh_db          = NULL;
static GtkWidget *g_ssh_list_box    = NULL;
static GtkWidget *g_ssh_count_lbl   = NULL;
static GtkWidget *g_ssh_search      = NULL;

static void
ssh_conn_free(SSHConn *c)
{
    if (c == NULL) return;
    g_free(c->id); g_free(c->name); g_free(c->host);
    g_free(c->user); g_free(c->auth_type); g_free(c->credential);
    g_free(c->group_name);
    g_free(c);
}

static char *
get_ssh_db_path(void)
{
    char *dir = get_app_data_dir();
    char *path = g_strdup_printf("%s/ssh.db", dir);
    g_free(dir);
    return path;
}

static int
ssh_db_init(void)
{
    char *dir = get_app_data_dir();
    ensure_dir(dir);
    g_free(dir);

    char *path = get_ssh_db_path();
    int rc = sqlite3_open(path, &g_ssh_db);
    g_free(path);
    if (rc != SQLITE_OK) {
        if (g_ssh_db) { sqlite3_close(g_ssh_db); g_ssh_db = NULL; }
        return -1;
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS ssh_connections ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  host TEXT NOT NULL,"
        "  port INTEGER DEFAULT 22,"
        "  user TEXT NOT NULL,"
        "  auth_type TEXT DEFAULT 'key',"
        "  credential TEXT DEFAULT '',"
        "  group_name TEXT DEFAULT '',"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");";
    sqlite3_exec(g_ssh_db, schema, NULL, NULL, NULL);
    return 0;
}

static char *
ssh_new_uuid(void)
{
    static unsigned int seed = 0;
    if (seed == 0) seed = (unsigned int)time(NULL);
    unsigned r1 = rand_r(&seed);
    unsigned r2 = rand_r(&seed);
    return g_strdup_printf("%08x-%04x-%04x-%04x-%012x%08x",
        r1, r2 >> 8, (r2 & 0xFF) | 0x40, (r1 >> 4) & 0x3FFF | 0x8000,
        (unsigned)time(NULL), r1 ^ r2);
}

static GPtrArray *
ssh_list_connections(const char *search)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(
        (GDestroyNotify)ssh_conn_free);
    if (g_ssh_db == NULL) return arr;

    const char *base_sql =
        "SELECT id, name, host, port, user, auth_type, credential, "
        "group_name, created_at, updated_at FROM ssh_connections";
    char sql[2048];
    if (search != NULL && *search != '\0') {
        char *esc = g_strescape(search, NULL);
        snprintf(sql, sizeof(sql),
            "%s WHERE name LIKE '%%%s%%' OR host LIKE '%%%s%%' "
            "OR user LIKE '%%%s%%' OR group_name LIKE '%%%s%%' "
            "ORDER BY group_name, updated_at DESC;",
            base_sql, esc, esc, esc, esc);
        g_free(esc);
    } else {
        snprintf(sql, sizeof(sql),
            "%s ORDER BY group_name, updated_at DESC;", base_sql);
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_ssh_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SSHConn *c = g_new0(SSHConn, 1);
            c->id         = g_strdup((const char *)sqlite3_column_text(stmt, 0));
            c->name       = g_strdup((const char *)sqlite3_column_text(stmt, 1));
            c->host       = g_strdup((const char *)sqlite3_column_text(stmt, 2));
            c->port       = sqlite3_column_int(stmt, 3);
            c->user       = g_strdup((const char *)sqlite3_column_text(stmt, 4));
            c->auth_type  = g_strdup((const char *)sqlite3_column_text(stmt, 5));
            c->credential = g_strdup((const char *)sqlite3_column_text(stmt, 6));
            c->group_name = g_strdup((const char *)sqlite3_column_text(stmt, 7));
            c->created_at = sqlite3_column_int64(stmt, 8);
            c->updated_at = sqlite3_column_int64(stmt, 9);
            g_ptr_array_add(arr, c);
        }
    }
    sqlite3_finalize(stmt);
    return arr;
}

static int
ssh_insert(SSHConn *c, char **err_out)
{
    if (g_ssh_db == NULL) return -1;
    const char *sql =
        "INSERT INTO ssh_connections (id,name,host,port,user,auth_type,"
        "credential,group_name,created_at,updated_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_ssh_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        if (err_out) *err_out = g_strdup(sqlite3_errmsg(g_ssh_db));
        return -1;
    }
    gint64 now = (gint64)time(NULL);
    sqlite3_bind_text(stmt, 1, c->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, c->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, c->host, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4, c->port);
    sqlite3_bind_text(stmt, 5, c->user, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, c->auth_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, c->credential, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, c->group_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 9, now);
    sqlite3_bind_int64(stmt,10, now);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && err_out)
        *err_out = g_strdup(sqlite3_errmsg(g_ssh_db));
    return 0;
}

static int
ssh_update(SSHConn *c, char **err_out)
{
    if (g_ssh_db == NULL) return -1;
    const char *sql =
        "UPDATE ssh_connections SET name=?,host=?,port=?,user=?,auth_type=?,"
        "credential=?,group_name=?,updated_at=? WHERE id=?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_ssh_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, c->name,      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, c->host,      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 3, c->port);
    sqlite3_bind_text(stmt, 4, c->user,      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, c->auth_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, c->credential,-1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, c->group_name,-1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, (gint64)time(NULL));
    sqlite3_bind_text(stmt, 9, c->id,        -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && err_out) *err_out = g_strdup(sqlite3_errmsg(g_ssh_db));
    return 0;
}

static int
ssh_delete(const char *id)
{
    if (g_ssh_db == NULL) return -1;
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(g_ssh_db,
        "DELETE FROM ssh_connections WHERE id=?;", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return 0;
}

/* Parse ~/.ssh/config and import Host entries */
static void
ssh_import_config(void)
{
    const char *home = g_get_home_dir();
    if (home == NULL) return;
    char *cfg_path = g_build_filename(home, ".ssh", "config", NULL);

    gchar *data = NULL;
    gsize len = 0;
    if (!g_file_get_contents(cfg_path, &data, &len, NULL)) {
        g_free(cfg_path);
        return;
    }

    gchar **lines = g_strsplit(data, "\n", -1);
    g_free(data);

    char *cur_host = NULL, *cur_hostname = NULL, *cur_user = NULL;
    int   cur_port = 22;
    char *cur_key  = NULL;
    int   imported = 0;

    for (int i = 0; lines[i] != NULL; i++) {
        char *line = lines[i];
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#' || *line == '\0') continue;

        char *space = strchr(line, ' ');
        if (space == NULL) continue;
        *space = '\0';
        char *key = line, *val = space + 1;
        while (*val == ' ') val++;

        gboolean is_host = g_ascii_strcasecmp(key, "Host") == 0;
        if (is_host && cur_host != NULL && strcmp(cur_host, "*") != 0) {
            SSHConn c = {
                .id         = ssh_new_uuid(),
                .name       = g_strdup(cur_host),
                .host       = g_strdup(cur_hostname ? cur_hostname : cur_host),
                .port       = cur_port,
                .user       = g_strdup(cur_user ? cur_user : ""),
                .auth_type  = g_strdup("key"),
                .credential = g_strdup(cur_key ? cur_key : ""),
                .group_name = g_strdup(""),
            };
            ssh_insert(&c, NULL);
            ssh_conn_free(&c);
            imported++;
        }

        if (is_host) {
            g_free(cur_host); cur_host = g_strdup(val);
            g_free(cur_hostname); cur_hostname = NULL;
            g_free(cur_user); cur_user = NULL;
            cur_port = 22;
            g_free(cur_key); cur_key = NULL;
        } else if (g_ascii_strcasecmp(key, "HostName") == 0 ||
                   g_ascii_strcasecmp(key, "Hostname") == 0) {
            g_free(cur_hostname); cur_hostname = g_strdup(val);
        } else if (g_ascii_strcasecmp(key, "User") == 0) {
            g_free(cur_user); cur_user = g_strdup(val);
        } else if (g_ascii_strcasecmp(key, "Port") == 0) {
            cur_port = atoi(val);
        } else if (g_ascii_strcasecmp(key, "IdentityFile") == 0) {
            if (*val == '~') {
                g_free(cur_key);
                cur_key = g_build_filename(home, val + 1);
            } else {
                g_free(cur_key); cur_key = g_strdup(val);
            }
        }
    }

    if (cur_host != NULL && strcmp(cur_host, "*") != 0) {
        SSHConn c = {
            .id         = ssh_new_uuid(),
            .name       = g_strdup(cur_host),
            .host       = g_strdup(cur_hostname ? cur_hostname : cur_host),
            .port       = cur_port,
            .user       = g_strdup(cur_user ? cur_user : ""),
            .auth_type  = g_strdup("key"),
            .credential = g_strdup(cur_key ? cur_key : ""),
            .group_name = g_strdup(""),
        };
        ssh_insert(&c, NULL);
        ssh_conn_free(&c);
        imported++;
    }

    g_free(cur_host); g_free(cur_hostname); g_free(cur_user); g_free(cur_key);
    g_strfreev(lines);
    g_free(cfg_path);
    set_status("✓ Imported %d SSH connections from ~/.ssh/config", imported);
}

static void refresh_ssh_list(void);

typedef struct {
    int    is_new;
    char  *id;
    char  *name;
    char  *host;
    int    port;
    char  *user;
    char  *auth_type;
    char  *credential;
    char  *group_name;
} SSHForm;

static gboolean
run_ssh_dialog(GtkWindow *parent, SSHForm *form)
{
    const char *title = form->is_new ? "Add SSH Connection" : "Edit SSH Connection";
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title, parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 540, 380);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    install_shortcuts(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    GtkWidget *en_name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_name), "Connection name");
    if (form->name) gtk_entry_set_text(GTK_ENTRY(en_name), form->name);

    GtkWidget *en_host = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_host), "hostname or IP");
    if (form->host) gtk_entry_set_text(GTK_ENTRY(en_host), form->host);

    GtkWidget *en_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_user), "username");
    if (form->user) gtk_entry_set_text(GTK_ENTRY(en_user), form->user);

    GtkWidget *en_port = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(en_port), form->port);

    GtkWidget *en_group = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_group), "optional group");
    if (form->group_name) gtk_entry_set_text(GTK_ENTRY(en_group), form->group_name);

    const char *items[] = {"key", "password", NULL};
    GtkWidget *cmb_auth = gtk_combo_box_text_new();
    for (int i = 0; items[i] != NULL; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cmb_auth), items[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(cmb_auth), 0);

    GtkWidget *lbl_fields[] = {
        gtk_label_new("Name"), gtk_label_new("Host"), gtk_label_new("User"),
        gtk_label_new("Port"), gtk_label_new("Group"),
        gtk_label_new("Auth Type"),
    };
    GtkWidget *inputs[] = {en_name, en_host, en_user, en_port, en_group, cmb_auth};
    for (int i = 0; i < 6; i++) {
        gtk_widget_set_halign(lbl_fields[i], GTK_ALIGN_END);
        gtk_widget_set_hexpand(inputs[i], TRUE);
        gtk_grid_attach(GTK_GRID(grid), lbl_fields[i], 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), inputs[i],     1, i, 1, 1);
    }

    gtk_widget_show_all(dialog);
    gboolean saved = FALSE;
    while (TRUE) {
        if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) break;
        const char *n = gtk_entry_get_text(GTK_ENTRY(en_name));
        const char *h = gtk_entry_get_text(GTK_ENTRY(en_host));
        const char *u = gtk_entry_get_text(GTK_ENTRY(en_user));
        if (n == NULL || *n == '\0' || h == NULL || *h == '\0') {
            show_error(dialog, "Name and Host are required.");
            continue;
        }
        g_free(form->name); form->name = g_strdup(n);
        g_free(form->host); form->host = g_strdup(h);
        g_free(form->user); form->user = g_strdup(u && *u ? u : "root");
        form->port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(en_port));
        g_free(form->group_name); form->group_name = g_strdup(gtk_entry_get_text(GTK_ENTRY(en_group)));
        g_free(form->auth_type);  form->auth_type  = g_strdup(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cmb_auth)));
        saved = TRUE;
        break;
    }
    gtk_widget_destroy(dialog);
    return saved;
}

static void
on_ssh_connect_clicked(GtkButton *btn, gpointer user_data)
{
    SSHConn *c = (SSHConn *)user_data;
    (void)btn;
    char port_str[8]; snprintf(port_str, sizeof(port_str), "%d", c->port);
    pid_t pid = fork();
    if (pid == 0) {
        if (c->credential && *c->credential && g_strcmp0(c->auth_type, "key") == 0)
            execlp("ssh", "ssh", "-p", port_str, "-i", c->credential,
                   c->user && *c->user ? c->user : "root",
                   c->host, (char *)NULL);
        else
            execlp("ssh", "ssh", "-p", port_str,
                   c->user && *c->user ? c->user : "root",
                   c->host, (char *)NULL);
        _exit(1);
    }
    set_status("Connecting to %s via SSH...", c->host);
}

static void
on_ssh_edit_clicked(GtkButton *btn, gpointer user_data)
{
    SSHConn *c = (SSHConn *)user_data;
    SSHForm form = {
        .is_new = 0,
        .id = c->id, .name = g_strdup(c->name), .host = g_strdup(c->host),
        .port = c->port, .user = g_strdup(c->user ? c->user : ""),
        .auth_type = g_strdup("key"), .credential = g_strdup(c->credential ? c->credential : ""),
        .group_name = g_strdup(c->group_name),
    };
    if (run_ssh_dialog(GTK_WINDOW(g_main_window), &form)) {
        SSHConn updated = {
            .id = form.id, .name = form.name, .host = form.host,
            .port = form.port, .user = form.user, .auth_type = form.auth_type,
            .credential = form.credential, .group_name = form.group_name,
        };
        ssh_update(&updated, NULL);
        set_status("✓ Updated '%s'", form.name);
        refresh_ssh_list();
    }
    g_free(form.name); g_free(form.host); g_free(form.user);
    g_free(form.auth_type); g_free(form.credential); g_free(form.group_name);
}

static void
on_ssh_delete_clicked(GtkButton *btn, gpointer user_data)
{
    SSHConn *c = (SSHConn *)user_data;
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "Delete SSH connection '%s'?", c->name);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        ssh_delete(c->id);
        set_status("Deleted '%s'", c->name);
        refresh_ssh_list();
    }
    gtk_widget_destroy(dialog);
}

static GtkWidget *
create_ssh_row(SSHConn *c)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox,   8);
    gtk_widget_set_margin_top(hbox,   6);
    gtk_widget_set_margin_bottom(hbox, 6);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);

    char *markup = g_strdup_printf(
        "<b>%s</b>  <span size=\"small\" foreground=\"#86868b\">(%s)</span>",
        c->name ? c->name : c->host, c->group_name ? c->group_name : "");
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "account-name");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    char *meta = g_strdup_printf("%s@%s:%d",
        c->user && *c->user ? c->user : "root", c->host, c->port);
    GtkWidget *meta_lbl = gtk_label_new(meta);
    g_free(meta);
    gtk_widget_set_halign(meta_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(meta_lbl), "account-meta");
    gtk_box_pack_start(GTK_BOX(vbox), meta_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    GtkWidget *conn_btn = gtk_button_new_with_label("Connect");
    gtk_style_context_add_class(gtk_widget_get_style_context(conn_btn), "open-button");
    g_signal_connect(conn_btn, "clicked", G_CALLBACK(on_ssh_connect_clicked), c);
    gtk_box_pack_start(GTK_BOX(hbox), conn_btn, FALSE, FALSE, 0);

    GtkWidget *edit_btn = gtk_button_new_with_label("Edit");
    gtk_style_context_add_class(gtk_widget_get_style_context(edit_btn), "edit-button");
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_ssh_edit_clicked), c);
    gtk_box_pack_start(GTK_BOX(hbox), edit_btn, FALSE, FALSE, 0);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_style_context_add_class(gtk_widget_get_style_context(del_btn), "delete-button");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_ssh_delete_clicked), c);
    gtk_box_pack_start(GTK_BOX(hbox), del_btn, FALSE, FALSE, 0);

    return hbox;
}

static void
on_ssh_search_changed(GtkSearchEntry *entry, gpointer data)
{
    (void)entry; (void)data;
    refresh_ssh_list();
}

static void
on_ssh_add_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    SSHForm form = { .is_new = 1, .port = 22, .auth_type = g_strdup("key"),
        .credential = g_strdup(""), .group_name = g_strdup("") };
    if (run_ssh_dialog(GTK_WINDOW(g_main_window), &form)) {
        char *uuid = ssh_new_uuid();
        SSHConn c = {
            .id = uuid, .name = form.name, .host = form.host,
            .port = form.port, .user = form.user, .auth_type = form.auth_type,
            .credential = form.credential, .group_name = form.group_name,
        };
        ssh_insert(&c, NULL);
        ssh_conn_free(&c);
        g_free(uuid);
        set_status("✓ Added '%s'", form.name);
        refresh_ssh_list();
    }
    g_free(form.name); g_free(form.host); g_free(form.user);
    g_free(form.auth_type); g_free(form.credential); g_free(form.group_name);
}

static void
on_ssh_import_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    ssh_import_config();
    refresh_ssh_list();
}

static void
refresh_ssh_list(void)
{
    if (g_ssh_list_box == NULL) return;

    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_ssh_list_box));
    for (GList *l = kids; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    const char *search = g_ssh_search
        ? gtk_entry_get_text(GTK_ENTRY(g_ssh_search)) : NULL;
    GPtrArray *conns = ssh_list_connections(search);

    if (g_ssh_count_lbl) {
        char *txt = g_strdup_printf("%d connections", conns->len);
        gtk_label_set_text(GTK_LABEL(g_ssh_count_lbl), txt);
        g_free(txt);
    }

    if (conns->len == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        GtkWidget *empty = gtk_label_new("No SSH connections. Add one or import from ~/.ssh/config.");
        gtk_style_context_add_class(gtk_widget_get_style_context(empty), "empty-label");
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(row), empty);
        gtk_container_add(GTK_CONTAINER(g_ssh_list_box), row);
    } else {
        for (guint i = 0; i < conns->len; i++) {
            SSHConn *c = g_ptr_array_index(conns, i);
            g_ptr_array_index(conns, i) = NULL;
            GtkWidget *r = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r), FALSE);
            gtk_container_add(GTK_CONTAINER(r), create_ssh_row(c));
            gtk_container_add(GTK_CONTAINER(g_ssh_list_box), r);
            g_object_set_data_full(G_OBJECT(r), "conn", c,
                (GDestroyNotify)ssh_conn_free);
        }
    }
    g_ptr_array_free(conns, TRUE);
    gtk_widget_show_all(g_ssh_list_box);
}

static GtkWidget *
build_ssh_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "header-box");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(header_text, TRUE);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span font_weight=\"bold\" size=\"12000\">SSH Connections</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "title-label");
    gtk_box_pack_start(GTK_BOX(header_text), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("Manage remote SSH connections");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(header_text), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), header_text, TRUE, TRUE, 0);

    g_ssh_count_lbl = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(g_ssh_count_lbl), "subtitle-label");
    gtk_box_pack_end(GTK_BOX(header), g_ssh_count_lbl, FALSE, FALSE, 0);

    GtkWidget *import_btn = gtk_button_new_with_label("Import ~/.ssh/config");
    gtk_style_context_add_class(gtk_widget_get_style_context(import_btn), "edit-button");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_ssh_import_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(header), import_btn, FALSE, FALSE, 0);

    GtkWidget *add_btn = gtk_button_new_with_label("+ Add");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "add-button");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_ssh_add_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(header), add_btn, FALSE, FALSE, 0);

    GtkWidget *search_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(search_bar, 12); gtk_widget_set_margin_end(search_bar, 12);
    gtk_widget_set_margin_top(search_bar, 8); gtk_widget_set_margin_bottom(search_bar, 8);
    g_ssh_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_ssh_search), "Search by name, host, or group...");
    gtk_widget_set_hexpand(g_ssh_search, TRUE);
    g_signal_connect(g_ssh_search, "search-changed",
        G_CALLBACK(on_ssh_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(search_bar), g_ssh_search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), search_bar, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    g_ssh_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_ssh_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), g_ssh_list_box);

    ssh_db_init();
    refresh_ssh_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  PTY Terminal tab using libvte
 *  Multiple VteTerminal instances with persistent sessions
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    char      *id;
    char      *name;
    VteTerminal *terminal;
    GtkWidget *scrolled;
    gboolean   is_running;
    GPid       child_pid;
} TermSession;

static GtkWidget    *g_term_container  = NULL;
static GtkWidget    *g_term_tabs       = NULL;
static GPtrArray    *g_term_sessions   = NULL;
static TermSession  *g_active_term     = NULL;
static int           g_term_counter    = 0;

static void
term_session_free(TermSession *ts)
{
    if (ts == NULL) return;
    g_free(ts->id);
    g_free(ts->name);
    g_free(ts);
}

static void
on_term_child_exited(VteTerminal *terminal, gint status, gpointer user_data)
{
    TermSession *ts = (TermSession *)user_data;
    (void)terminal;
    ts->is_running = FALSE;
    set_status("Terminal '%s' exited (status %d)", ts->name, status);
}

static void
term_launch(TermSession *ts, const char *command, const char *cwd)
{
    if (ts->terminal == NULL) return;

    char *argv[] = {"/bin/bash", "-c", (char *)command, NULL};
    char *fallback_argv[] = {"/bin/sh", "-c", (char *)command, NULL};

    vte_terminal_spawn_async(
        ts->terminal,
        VTE_PTY_DEFAULT,
        cwd ? cwd : g_get_home_dir(),
        g_file_test("/bin/bash", G_FILE_TEST_IS_EXECUTABLE) ? argv : fallback_argv,
        NULL, 0,
        G_SPAWN_DEFAULT,
        NULL, NULL, NULL,
        -1, NULL, NULL, NULL);

    ts->is_running = TRUE;
}

static TermSession *
term_create_session(const char *name)
{
    TermSession *ts = g_new0(TermSession, 1);
    ts->id   = g_strdup_printf("term-%d", ++g_term_counter);
    ts->name = g_strdup(name ? name : ts->id);

    ts->terminal = VTE_TERMINAL(vte_terminal_new());
    vte_terminal_set_scrollback_lines(ts->terminal, 100000);
    vte_terminal_set_font_scale(ts->terminal, 1.0);

    PangoFontDescription *font_desc = pango_font_description_from_string(
        "Monospace 12");
    vte_terminal_set_font(ts->terminal, font_desc);
    pango_font_description_free(font_desc);

    GtkWidget *scrolled = gtk_scrolled_window_new(
        gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(ts->terminal)),
        gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(ts->terminal)));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(ts->terminal));
    ts->scrolled = scrolled;

    g_signal_connect(ts->terminal, "child-exited",
        G_CALLBACK(on_term_child_exited), ts);

    if (g_term_sessions == NULL)
        g_term_sessions = g_ptr_array_new_with_free_func(
            (GDestroyNotify)term_session_free);
    g_ptr_array_add(g_term_sessions, ts);
    return ts;
}

static void
term_switch_to(TermSession *ts)
{
    if (g_term_container == NULL || ts == NULL) return;

    if (g_active_term != NULL && g_active_term != ts) {
        gtk_container_remove(GTK_CONTAINER(g_term_container),
            g_active_term->scrolled);
    }

    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_term_container));
    for (GList *l = kids; l; l = l->next)
        gtk_container_remove(GTK_CONTAINER(g_term_container), GTK_WIDGET(l->data));
    g_list_free(kids);

    gtk_container_add(GTK_CONTAINER(g_term_container), ts->scrolled);
    gtk_widget_show_all(ts->scrolled);
    gtk_widget_grab_focus(GTK_WIDGET(ts->terminal));
    g_active_term = ts;
    set_status("Terminal: %s", ts->name);
}

static void
on_term_tab_switch(GtkNotebook *nb, GtkWidget *page, guint page_num,
                   gpointer data)
{
    (void)nb; (void)page; (void)data;
    if (g_term_sessions == NULL || page_num >= g_term_sessions->len) return;
    TermSession *ts = g_ptr_array_index(g_term_sessions, page_num);
    term_switch_to(ts);
}

static void
on_term_new_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    TermSession *ts = term_create_session(NULL);
    term_launch(ts, "bash", g_get_home_dir());

    GtkWidget *tab_label = gtk_label_new(ts->name);
    gtk_notebook_append_page(GTK_NOTEBOOK(g_term_tabs),
        gtk_label_new(""), tab_label);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(g_term_tabs),
        gtk_notebook_get_n_pages(GTK_NOTEBOOK(g_term_tabs)) - 1);

    term_switch_to(ts);
    set_status("New terminal session: %s", ts->name);
}

static void
run_tool_in_terminal(GtkButton *btn, const char *tool_name, const char *cmd)
{
    (void)btn;
    TermSession *ts = term_create_session(tool_name);
    term_launch(ts, cmd, g_get_home_dir());

    GtkWidget *tab_label = gtk_label_new(tool_name);
    gtk_notebook_append_page(GTK_NOTEBOOK(g_term_tabs),
        gtk_label_new(""), tab_label);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(g_term_tabs),
        gtk_notebook_get_n_pages(GTK_NOTEBOOK(g_term_tabs)) - 1);

    term_switch_to(ts);
    set_status("Launching %s in terminal", tool_name);
}

static void on_term_launch_bash(GtkButton *btn, gpointer d)
{ run_tool_in_terminal(btn, "bash", "bash"); }

static void on_term_launch_opencode(GtkButton *btn, gpointer d)
{ run_tool_in_terminal(btn, "opencode", "opencode"); }

static void on_term_launch_claude(GtkButton *btn, gpointer d)
{ run_tool_in_terminal(btn, "claude", "claude"); }

static void on_term_launch_htop(GtkButton *btn, gpointer d)
{ run_tool_in_terminal(btn, "htop", "htop"); }

static GtkWidget *
build_terminal_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(toolbar), "header-box");
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 4);
    gtk_widget_set_margin_bottom(toolbar, 4);

    GtkWidget *new_btn = gtk_button_new_with_label("+ New Terminal");
    gtk_style_context_add_class(gtk_widget_get_style_context(new_btn), "add-button");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_term_new_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), new_btn, FALSE, FALSE, 0);

    static const struct { const char *label; GCallback cb; } tools[] = {
        {"bash",     G_CALLBACK(on_term_launch_bash)},
        {"opencode", G_CALLBACK(on_term_launch_opencode)},
        {"claude",   G_CALLBACK(on_term_launch_claude)},
        {"htop",     G_CALLBACK(on_term_launch_htop)},
        {NULL, NULL},
    };
    for (int i = 0; tools[i].label != NULL; i++) {
        GtkWidget *btn = gtk_button_new_with_label(tools[i].label);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(btn), "edit-button");
        g_signal_connect(btn, "clicked", tools[i].cb, NULL);
        gtk_box_pack_start(GTK_BOX(toolbar), btn, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);

    /* Term tabs (for switching between sessions) */
    g_term_tabs = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(g_term_tabs), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(g_term_tabs), FALSE);
    g_signal_connect(g_term_tabs, "switch-page",
        G_CALLBACK(on_term_tab_switch), NULL);
    gtk_box_pack_start(GTK_BOX(root), g_term_tabs, FALSE, FALSE, 0);
    gtk_widget_hide(g_term_tabs);

    /* Container for the active VteTerminal */
    g_term_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(root), g_term_container, TRUE, TRUE, 0);

    /* Auto-create a bash terminal on init */
    TermSession *ts = term_create_session("bash");
    term_launch(ts, "bash", g_get_home_dir());
    GtkWidget *empty_label = gtk_label_new("");
    gtk_notebook_append_page(GTK_NOTEBOOK(g_term_tabs), empty_label,
        gtk_label_new("bash"));
    term_switch_to(ts);

    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Chat Web tab using webkit2gtk
 *  Embedded browser for AI chat providers with CRUD for providers
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    char *id;
    char *name;
    char *url;
    int   sort_order;
} ChatProvider;

static sqlite3   *g_chat_db          = NULL;
static GtkWidget *g_chat_list_box    = NULL;
static GtkWidget *g_chat_webview_box = NULL;
static WebKitWebView *g_webview      = NULL;
static GtkWidget *g_chat_url_lbl     = NULL;
static ChatProvider *g_active_chat   = NULL;

static void
chat_provider_free(ChatProvider *p)
{
    if (p == NULL) return;
    g_free(p->id); g_free(p->name); g_free(p->url);
    g_free(p);
}

static char *
get_chat_db_path(void)
{
    char *dir = get_app_data_dir();
    char *path = g_strdup_printf("%s/chat_providers.db", dir);
    g_free(dir);
    return path;
}

static int
chat_db_init(void)
{
    char *dir = get_app_data_dir();
    ensure_dir(dir);
    g_free(dir);

    char *path = get_chat_db_path();
    int rc = sqlite3_open(path, &g_chat_db);
    g_free(path);
    if (rc != SQLITE_OK) {
        if (g_chat_db) { sqlite3_close(g_chat_db); g_chat_db = NULL; }
        return -1;
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS chat_providers ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  url TEXT NOT NULL UNIQUE,"
        "  sort_order INTEGER DEFAULT 0,"
        "  created_at INTEGER,"
        "  updated_at INTEGER"
        ");";
    sqlite3_exec(g_chat_db, schema, NULL, NULL, NULL);

    /* Seed defaults if table is empty */
    sqlite3_stmt *cnt = NULL;
    int count = 0;
    if (sqlite3_prepare_v2(g_chat_db,
            "SELECT COUNT(*) FROM chat_providers;",
            -1, &cnt, NULL) == SQLITE_OK) {
        if (sqlite3_step(cnt) == SQLITE_ROW)
            count = sqlite3_column_int(cnt, 0);
        sqlite3_finalize(cnt);
    }
    if (count == 0) {
        static const struct { const char *name; const char *url; } defaults[] = {
            {"DeepSeek",    "https://chat.deepseek.com/"},
            {"Qwen",        "https://chat.qwen.ai/"},
            {"ChatGPT",     "https://chatgpt.com/"},
            {"AI Studio",   "https://aistudio.google.com/"},
            {"Kimi",        "https://kimi.moonshot.cn/"},
            {"MiniMax",     "https://chat.minimaxi.com/"},
            {"Poe",         "https://poe.com/"},
            {"Mistral",     "https://chat.mistral.ai/"},
            {"Groq",        "https://chat.groq.com/"},
            {"Grok",        "https://grok.com/"},
            {NULL, NULL},
        };
        sqlite3_stmt *ins = NULL;
        sqlite3_prepare_v2(g_chat_db,
            "INSERT INTO chat_providers (id,name,url,sort_order,created_at,updated_at) "
            "VALUES (?,?,?,?,?,?);", -1, &ins, NULL);
        gint64 now = (gint64)time(NULL);
        for (int i = 0; defaults[i].name != NULL; i++) {
            char *uuid = ssh_new_uuid();
            sqlite3_bind_text(ins, 1, uuid, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 2, defaults[i].name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 3, defaults[i].url, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (ins, 4, i);
            sqlite3_bind_int64(ins, 5, now);
            sqlite3_bind_int64(ins, 6, now);
            sqlite3_step(ins);
            sqlite3_reset(ins);
            g_free(uuid);
        }
        sqlite3_finalize(ins);
    }
    return 0;
}

static GPtrArray *
chat_list_providers(void)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(
        (GDestroyNotify)chat_provider_free);
    if (g_chat_db == NULL) return arr;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_chat_db,
            "SELECT id,name,url FROM chat_providers ORDER BY sort_order;",
            -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ChatProvider *p = g_new0(ChatProvider, 1);
            p->id   = g_strdup((const char *)sqlite3_column_text(stmt, 0));
            p->name = g_strdup((const char *)sqlite3_column_text(stmt, 1));
            p->url  = g_strdup((const char *)sqlite3_column_text(stmt, 2));
            g_ptr_array_add(arr, p);
        }
    }
    sqlite3_finalize(stmt);
    return arr;
}

static void
chat_insert(const char *name, const char *url)
{
    if (g_chat_db == NULL) return;
    char *uuid = ssh_new_uuid();
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(g_chat_db,
        "INSERT INTO chat_providers (id,name,url,sort_order,created_at,updated_at) "
        "VALUES (?,?,?,?,?,?);", -1, &stmt, NULL);
    gint64 now = (gint64)time(NULL);
    sqlite3_bind_text(stmt, 1, uuid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, url,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4, 999);
    sqlite3_bind_int64(stmt, 5, now);
    sqlite3_bind_int64(stmt, 6, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    g_free(uuid);
}

static void
chat_delete(const char *id)
{
    if (g_chat_db == NULL) return;
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(g_chat_db,
        "DELETE FROM chat_providers WHERE id=?;", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void refresh_chat_list(void);

static void
on_chat_web_load_changed(WebKitWebView *wv, WebKitLoadEvent event,
                          gpointer data)
{
    (void)data;
    if (g_chat_url_lbl == NULL) return;
    const char *uri = webkit_web_view_get_uri(wv);
    if (uri && event == WEBKIT_LOAD_COMMITTED)
        gtk_label_set_text(GTK_LABEL(g_chat_url_lbl), uri);
}

static void
on_chat_open_clicked(GtkButton *btn, gpointer user_data)
{
    ChatProvider *p = (ChatProvider *)user_data;
    (void)btn;
    if (g_webview == NULL) return;
    webkit_web_view_load_uri(g_webview, p->url);
    g_free(g_active_chat);
    g_active_chat = g_new0(ChatProvider, 1);
    g_active_chat->name = g_strdup(p->name);
    g_active_chat->url  = g_strdup(p->url);
    if (g_chat_url_lbl) gtk_label_set_text(GTK_LABEL(g_chat_url_lbl), p->url);
    set_status("Loading %s: %s", p->name, p->url);
}

static void
on_chat_delete_clicked(GtkButton *btn, gpointer user_data)
{
    ChatProvider *p = (ChatProvider *)user_data;
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "Delete chat provider '%s'?", p->name);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        chat_delete(p->id);
        refresh_chat_list();
    }
    gtk_widget_destroy(dialog);
}

static GtkWidget *
create_chat_row(ChatProvider *p)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox,   12);
    gtk_widget_set_margin_end(hbox,     8);
    gtk_widget_set_margin_top(hbox,     4);
    gtk_widget_set_margin_bottom(hbox,  4);

    GtkWidget *name_lbl = gtk_label_new(p->name);
    gtk_widget_set_hexpand(name_lbl, TRUE);
    gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(name_lbl), "account-name");
    gtk_box_pack_start(GTK_BOX(hbox), name_lbl, TRUE, TRUE, 0);

    GtkWidget *url_lbl = gtk_label_new(p->url);
    gtk_widget_set_halign(url_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(url_lbl), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(url_lbl), "account-meta");
    gtk_box_pack_start(GTK_BOX(hbox), url_lbl, TRUE, TRUE, 0);

    GtkWidget *open_btn = gtk_button_new_with_label("Open");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(open_btn), "open-button");
    g_signal_connect(open_btn, "clicked",
        G_CALLBACK(on_chat_open_clicked), p);
    gtk_box_pack_start(GTK_BOX(hbox), open_btn, FALSE, FALSE, 0);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(del_btn), "delete-button");
    g_signal_connect(del_btn, "clicked",
        G_CALLBACK(on_chat_delete_clicked), p);
    gtk_box_pack_start(GTK_BOX(hbox), del_btn, FALSE, FALSE, 0);

    return hbox;
}

static void
on_chat_add_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Add Chat Provider",
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 440, 180);
    install_shortcuts(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    GtkWidget *en_name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_name), "e.g. ChatGPT");
    GtkWidget *en_url = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_url), "https://chatgpt.com/");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), en_name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("URL"),  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), en_url,  1, 1, 1, 1);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *n = gtk_entry_get_text(GTK_ENTRY(en_name));
        const char *u = gtk_entry_get_text(GTK_ENTRY(en_url));
        if (n && *n && u && *u) {
            chat_insert(n, u);
            refresh_chat_list();
        }
    }
    gtk_widget_destroy(dialog);
}

static void
refresh_chat_list(void)
{
    if (g_chat_list_box == NULL) return;

    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_chat_list_box));
    for (GList *l = kids; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    GPtrArray *providers = chat_list_providers();
    if (providers->len == 0) {
        GtkWidget *r = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r), FALSE);
        GtkWidget *e = gtk_label_new("No chat providers. Add one to get started.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_chat_list_box), r);
    } else {
        for (guint i = 0; i < providers->len; i++) {
            ChatProvider *p = g_ptr_array_index(providers, i);
            g_ptr_array_index(providers, i) = NULL;
            GtkWidget *r = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r), FALSE);
            gtk_container_add(GTK_CONTAINER(r), create_chat_row(p));
            gtk_container_add(GTK_CONTAINER(g_chat_list_box), r);
            g_object_set_data_full(G_OBJECT(r), "cp", p,
                (GDestroyNotify)chat_provider_free);
        }
    }
    g_ptr_array_free(providers, TRUE);
    gtk_widget_show_all(g_chat_list_box);
}

static void
on_chat_back_clicked(GtkButton *btn, gpointer data)
{ if (g_webview && webkit_web_view_can_go_back(g_webview))
      webkit_web_view_go_back(g_webview); }

static void
on_chat_fwd_clicked(GtkButton *btn, gpointer data)
{ if (g_webview && webkit_web_view_can_go_forward(g_webview))
      webkit_web_view_go_forward(g_webview); }

static void
on_chat_reload_clicked(GtkButton *btn, gpointer data)
{ if (g_webview) webkit_web_view_reload(g_webview); }

static GtkWidget *
build_chat_web_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Top nav bar */
    GtkWidget *nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(nav), "header-box");
    gtk_widget_set_margin_start(nav, 4); gtk_widget_set_margin_end(nav, 4);
    gtk_widget_set_margin_top(nav, 4); gtk_widget_set_margin_bottom(nav, 4);

    GtkWidget *back_btn = gtk_button_new_with_label("<");
    gtk_style_context_add_class(gtk_widget_get_style_context(back_btn), "edit-button");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_chat_back_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(nav), back_btn, FALSE, FALSE, 0);

    GtkWidget *fwd_btn = gtk_button_new_with_label(">");
    gtk_style_context_add_class(gtk_widget_get_style_context(fwd_btn), "edit-button");
    g_signal_connect(fwd_btn, "clicked", G_CALLBACK(on_chat_fwd_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(nav), fwd_btn, FALSE, FALSE, 0);

    GtkWidget *reload_btn = gtk_button_new_with_label("Reload");
    gtk_style_context_add_class(gtk_widget_get_style_context(reload_btn), "edit-button");
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_chat_reload_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(nav), reload_btn, FALSE, FALSE, 0);

    g_chat_url_lbl = gtk_label_new("Select a chat provider to load");
    gtk_widget_set_hexpand(g_chat_url_lbl, TRUE);
    gtk_widget_set_halign(g_chat_url_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(g_chat_url_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(g_chat_url_lbl), "account-meta");
    gtk_box_pack_start(GTK_BOX(nav), g_chat_url_lbl, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(root), nav, FALSE, FALSE, 0);

    /* Split view: sidebar (providers) + webview */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);

    /* Sidebar: providers list */
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 240, -1);

    GtkWidget *sidebar_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(sidebar_hdr, 8);
    gtk_widget_set_margin_end(sidebar_hdr, 8);
    gtk_widget_set_margin_top(sidebar_hdr, 8);
    gtk_widget_set_margin_bottom(sidebar_hdr, 4);

    GtkWidget *add_btn = gtk_button_new_with_label("+ Add Provider");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "add-button");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_chat_add_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(sidebar_hdr), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), sidebar_hdr, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    g_chat_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_chat_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), g_chat_list_box);
    gtk_box_pack_start(GTK_BOX(sidebar), scrolled, TRUE, TRUE, 0);

    gtk_paned_pack1(GTK_PANED(paned), sidebar, FALSE, FALSE);

    /* Web view */
    g_chat_webview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_webview = WEBKIT_WEB_VIEW(webkit_web_view_new());
    WebKitSettings *settings = webkit_web_view_get_settings(g_webview);
    webkit_settings_set_javascript_enabled(settings, TRUE);
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, TRUE);

    g_signal_connect(g_webview, "load-changed",
        G_CALLBACK(on_chat_web_load_changed), NULL);

    gtk_box_pack_start(GTK_BOX(g_chat_webview_box),
        GTK_WIDGET(g_webview), TRUE, TRUE, 0);

    gtk_paned_pack2(GTK_PANED(paned), g_chat_webview_box, TRUE, FALSE);

    chat_db_init();
    refresh_chat_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Kimchi Session Manager tab
 *  Similar JSONL indexer as Claude, with --prefix/suffix path encoding
 * ──────────────────────────────────────────────────────────────────────── */

typedef struct {
    char *id;
    char *project_name;
    char *decoded_path;
    char *ai_title;
    char *first_message;
    long  last_modified;
} KimchiSession;

static GtkWidget *g_kimchi_list_box = NULL;

static void refresh_kimchi_list(void);

static void kimchi_session_free(KimchiSession *s) {
    if (!s) return;
    g_free(s->id); g_free(s->project_name); g_free(s->decoded_path);
    g_free(s->ai_title); g_free(s->first_message); g_free(s);
}

static char *kimchi_decode_path(const char *encoded) {
    if (!encoded) return g_strdup("");
    GString *s = g_string_new(encoded);
    if (g_str_has_prefix(s->str, "--")) g_string_erase(s, 0, 2);
    if (g_str_has_suffix(s->str, "--"))
        g_string_truncate(s, s->len - 2);
    for (gsize i = 0; i < s->len; i++)
        if (s->str[i] == '-') s->str[i] = '/';
    char *result = g_strdup_printf("/%s", s->str);
    g_string_free(s, TRUE);
    return result;
}

static char *get_kimchi_projects_dir(void) {
    const char *home = g_get_home_dir();
    return g_strdup_printf("%s/.local/share/kimchi/projects", home);
}

static void
kimchi_scan_and_index(GPtrArray *arr)
{
    char *base = get_kimchi_projects_dir();
    DIR *d = opendir(base);
    if (!d) { g_free(base); return; }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char *subdir = g_build_filename(base, ent->d_name, NULL);
        DIR *sd = opendir(subdir);
        if (!sd) { g_free(subdir); continue; }
        struct dirent *se;
        while ((se = readdir(sd)) != NULL) {
            if (!g_str_has_suffix(se->d_name, ".jsonl")) continue;
            char *fp = g_build_filename(subdir, se->d_name, NULL);
            GStatBuf st;
            if (g_stat(fp, &st) != 0) { g_free(fp); continue; }
            char *title = NULL, *msg = NULL;
            claude_parse_jsonl(fp, &title, &msg);
            char *dotless = g_strdup(se->d_name);
            char *dot = strrchr(dotless, '.');
            if (dot) *dot = '\0';
            KimchiSession *s = g_new0(KimchiSession, 1);
            s->id            = dotless;
            s->project_name  = g_strdup(ent->d_name);
            s->decoded_path  = kimchi_decode_path(ent->d_name);
            s->ai_title      = title;
            s->first_message = msg;
            s->last_modified = (long)st.st_mtime;
            g_ptr_array_add(arr, s);
            g_free(fp);
        }
        closedir(sd);
        g_free(subdir);
    }
    closedir(d);
    g_free(base);
}

static void
on_kimchi_resume_clicked(GtkButton *btn, gpointer user_data)
{
    KimchiSession *s = (KimchiSession *)user_data;
    (void)btn;
    pid_t pid = fork();
    if (pid == 0) { execlp("kimchi", "kimchi", "--resume", s->id, (char *)NULL); _exit(1); }
    set_status("Launching kimchi: %s", s->ai_title ? s->ai_title : s->id);
}

static void
on_kimchi_open_dir_clicked(GtkButton *btn, gpointer user_data)
{ KimchiSession *s = (KimchiSession *)user_data; launch_editor(GTK_WIDGET(btn), s->decoded_path); }

static GtkWidget *create_kimchi_row(KimchiSession *s) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12); gtk_widget_set_margin_end(hbox, 8);
    gtk_widget_set_margin_top(hbox, 6); gtk_widget_set_margin_bottom(hbox, 6);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);
    const char *t = s->ai_title ? s->ai_title : (s->first_message ? s->first_message : "Kimchi Session");
    GtkWidget *tl = gtk_label_new(t);
    gtk_widget_set_halign(tl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(tl), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(tl), "account-name");
    gtk_box_pack_start(GTK_BOX(vbox), tl, FALSE, FALSE, 0);
    char *dp = display_path(s->decoded_path);
    char *rel = format_relative_time((long)s->last_modified * 1000);
    char *mk = g_strdup_printf("<span size=\"small\" foreground=\"#86868b\">%s · %s</span>", dp, rel);
    GtkWidget *ml = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ml), mk);
    g_free(mk); g_free(dp); g_free(rel);
    gtk_style_context_add_class(gtk_widget_get_style_context(ml), "account-meta");
    gtk_box_pack_start(GTK_BOX(vbox), ml, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    GtkWidget *rb = gtk_button_new_with_label("Resume");
    gtk_style_context_add_class(gtk_widget_get_style_context(rb), "open-button");
    g_signal_connect(rb, "clicked", G_CALLBACK(on_kimchi_resume_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), rb, FALSE, FALSE, 0);
    GtkWidget *ob = gtk_button_new_with_label("Open Dir");
    gtk_style_context_add_class(gtk_widget_get_style_context(ob), "edit-button");
    g_signal_connect(ob, "clicked", G_CALLBACK(on_kimchi_open_dir_clicked), s);
    gtk_box_pack_start(GTK_BOX(hbox), ob, FALSE, FALSE, 0);
    return hbox;
}

static void refresh_kimchi_list(void) {
    if (!g_kimchi_list_box) return;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_kimchi_list_box));
    for (GList *l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);
    GPtrArray *sessions = g_ptr_array_new_with_free_func((GDestroyNotify)kimchi_session_free);
    kimchi_scan_and_index(sessions);
    if (sessions->len == 0) {
        GtkWidget *r = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
        GtkWidget *e = gtk_label_new("No Kimchi sessions found.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_kimchi_list_box), r);
    } else {
        for (guint i = 0; i < sessions->len; i++) {
            KimchiSession *s = g_ptr_array_index(sessions, i);
            g_ptr_array_index(sessions, i) = NULL;
            GtkWidget *r = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
            gtk_container_add(GTK_CONTAINER(r), create_kimchi_row(s));
            gtk_container_add(GTK_CONTAINER(g_kimchi_list_box), r);
            g_object_set_data_full(G_OBJECT(r), "session", s, (GDestroyNotify)kimchi_session_free);
        }
    }
    g_ptr_array_free(sessions, TRUE);
    gtk_widget_show_all(g_kimchi_list_box);
}

static GtkWidget *build_kimchi_tab(void) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(hdr), "header-box");
    GtkWidget *ht = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(ht, TRUE);
    GtkWidget *t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span font_weight=\"bold\" size=\"12000\">Kimchi Sessions</span>");
    gtk_widget_set_halign(t, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "title-label");
    gtk_box_pack_start(GTK_BOX(ht), t, FALSE, FALSE, 0);
    GtkWidget *st = gtk_label_new("Indexed CLI sessions from ~/.local/share/kimchi/");
    gtk_widget_set_halign(st, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(st), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(ht), st, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hdr), ht, TRUE, TRUE, 0);
    GtkWidget *rescan = gtk_button_new_with_label("Rescan");
    gtk_style_context_add_class(gtk_widget_get_style_context(rescan), "edit-button");
    g_signal_connect_swapped(rescan, "clicked", G_CALLBACK(refresh_kimchi_list), NULL);
    gtk_box_pack_end(GTK_BOX(hdr), rescan, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), hdr, FALSE, FALSE, 0);
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    g_kimchi_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_kimchi_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sc), g_kimchi_list_box);
    gtk_box_pack_start(GTK_BOX(root), sc, TRUE, TRUE, 0);
    refresh_kimchi_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Mimo Session Manager tab (shell wrapper + JSON parse)
 *  Runs: mimo session list --format json
 * ──────────────────────────────────────────────────────────────────────── */

static GtkWidget *g_mimo_list_box = NULL;

static void refresh_mimo_list(void) {
    if (!g_mimo_list_box) return;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_mimo_list_box));
    for (GList *l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    gchar *out = NULL; gint exit_status = 0;
    gboolean ok = g_spawn_command_line_sync(
        "mimo session list --format json", &out, NULL, &exit_status, NULL);

    if (!ok || exit_status != 0 || !out) {
        GtkWidget *r = gtk_list_box_row_new();
        GtkWidget *e = gtk_label_new(ok ? "mimo command returned no data." :
                                     "mimo not found. Install: npm i -g mimo");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_mimo_list_box), r);
        g_free(out);
        gtk_widget_show_all(g_mimo_list_box);
        return;
    }

    /* Simple JSON array parser: line-based, extract id/title/directory */
    gchar **lines = g_strsplit(out, "\n", -1);
    g_free(out);
    int count = 0;

    for (int i = 0; lines[i] != NULL; i++) {
        char *id_str = strstr(lines[i], "\"id\"");
        if (!id_str) continue;
        GtkWidget *r = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(hbox, 12); gtk_widget_set_margin_end(hbox, 8);
        gtk_widget_set_margin_top(hbox, 6); gtk_widget_set_margin_bottom(hbox, 6);
        char *label_str = g_strdup_printf("Mimo session #%d", ++count);
        GtkWidget *lbl = gtk_label_new(label_str);
        g_free(label_str);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "account-name");
        gtk_box_pack_start(GTK_BOX(hbox), lbl, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(r), hbox);
        gtk_container_add(GTK_CONTAINER(g_mimo_list_box), r);
    }

    if (count == 0) {
        GtkWidget *r = gtk_list_box_row_new();
        GtkWidget *e = gtk_label_new("No mimo sessions found.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_mimo_list_box), r);
    }
    g_strfreev(lines);
    gtk_widget_show_all(g_mimo_list_box);
}

static GtkWidget *build_mimo_tab(void) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(hdr), "header-box");
    GtkWidget *t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span font_weight=\"bold\" size=\"12000\">Mimo Sessions</span>");
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "title-label");
    gtk_box_pack_start(GTK_BOX(hdr), t, TRUE, TRUE, 0);
    GtkWidget *rescan = gtk_button_new_with_label("Rescan");
    gtk_style_context_add_class(gtk_widget_get_style_context(rescan), "edit-button");
    g_signal_connect_swapped(rescan, "clicked", G_CALLBACK(refresh_mimo_list), NULL);
    gtk_box_pack_end(GTK_BOX(hdr), rescan, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), hdr, FALSE, FALSE, 0);
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    g_mimo_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_mimo_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sc), g_mimo_list_box);
    gtk_box_pack_start(GTK_BOX(root), sc, TRUE, TRUE, 0);
    refresh_mimo_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Freebuff Session Manager tab
 *  Scans ~/.config/manicode/projects/<projName>
 * ──────────────────────────────────────────────────────────────────────── */

static GtkWidget *g_freebuff_list_box = NULL;

static void refresh_freebuff_list(void) {
    if (!g_freebuff_list_box) return;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_freebuff_list_box));
    for (GList *l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    const char *home = g_get_home_dir();
    char *base = g_strdup_printf("%s/.config/manicode/projects", home);
    DIR *d = opendir(base);
    if (!d) {
        GtkWidget *r = gtk_list_box_row_new();
        GtkWidget *e = gtk_label_new("No Freebuff/Manicode projects found.\n"
            "Start a freebuff session first.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_freebuff_list_box), r);
        g_free(base);
        gtk_widget_show_all(g_freebuff_list_box);
        return;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char *proj_dir = g_build_filename(base, ent->d_name, NULL);
        if (!g_file_test(proj_dir, G_FILE_TEST_IS_DIR)) { g_free(proj_dir); continue; }
        GtkWidget *r = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(hbox, 12); gtk_widget_set_margin_end(hbox, 8);
        GtkWidget *nm = gtk_label_new(ent->d_name);
        gtk_widget_set_hexpand(nm, TRUE);
        gtk_widget_set_halign(nm, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(nm), "account-name");
        gtk_box_pack_start(GTK_BOX(hbox), nm, TRUE, TRUE, 0);
        GtkWidget *ob = gtk_button_new_with_label("Open Dir");
        gtk_style_context_add_class(gtk_widget_get_style_context(ob), "edit-button");
        g_signal_connect_data(ob, "clicked",
            G_CALLBACK(launch_editor), g_strdup(proj_dir), (GClosureNotify)g_free, 0);
        gtk_box_pack_start(GTK_BOX(hbox), ob, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(r), hbox);
        gtk_container_add(GTK_CONTAINER(g_freebuff_list_box), r);
        count++;
        g_free(proj_dir);
    }
    closedir(d);
    g_free(base);
    if (count == 0) {
        GtkWidget *r = gtk_list_box_row_new();
        GtkWidget *e = gtk_label_new("No Freebuff sessions found.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_freebuff_list_box), r);
    }
    gtk_widget_show_all(g_freebuff_list_box);
}

static GtkWidget *build_freebuff_tab(void) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(hdr), "header-box");
    GtkWidget *t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span font_weight=\"bold\" size=\"12000\">Freebuff Sessions</span>");
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "title-label");
    gtk_box_pack_start(GTK_BOX(hdr), t, TRUE, TRUE, 0);
    GtkWidget *rescan = gtk_button_new_with_label("Rescan");
    gtk_style_context_add_class(gtk_widget_get_style_context(rescan), "edit-button");
    g_signal_connect_swapped(rescan, "clicked", G_CALLBACK(refresh_freebuff_list), NULL);
    gtk_box_pack_end(GTK_BOX(hdr), rescan, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), hdr, FALSE, FALSE, 0);
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    g_freebuff_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_freebuff_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sc), g_freebuff_list_box);
    gtk_box_pack_start(GTK_BOX(root), sc, TRUE, TRUE, 0);
    refresh_freebuff_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Port Monitor tab
 *  Parses 'ss -tlnp' output for active TCP listening ports
 * ──────────────────────────────────────────────────────────────────────── */

static GtkWidget *g_port_list_box = NULL;

static void refresh_port_list(void) {
    if (!g_port_list_box) return;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_port_list_box));
    for (GList *l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    gchar *out = NULL; gint status = 0;
    gboolean ok = g_spawn_command_line_sync("ss -tlnp", &out, NULL, &status, NULL);
    if (!ok || !out) {
        /* Fallback: try netstat */
        g_free(out);
        ok = g_spawn_command_line_sync("netstat -tlnp", &out, NULL, &status, NULL);
    }
    if (!ok || !out) {
        GtkWidget *r = gtk_list_box_row_new();
        GtkWidget *e = gtk_label_new("Could not run ss or netstat.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_port_list_box), r);
        gtk_widget_show_all(g_port_list_box);
        return;
    }

    gchar **lines = g_strsplit(out, "\n", -1);
    g_free(out);
    int count = 0;

    for (int i = 0; lines[i] != NULL; i++) {
        if (lines[i][0] == '\0' || g_str_has_prefix(lines[i], "Netid")) continue;
        GtkWidget *r = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
        GtkWidget *lbl = gtk_label_new(lines[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_set_margin_start(lbl, 12); gtk_widget_set_margin_top(lbl, 3);
        gtk_widget_set_margin_bottom(lbl, 3);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "account-meta");
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_family_new("Monospace"));
        pango_attr_list_insert(attrs, pango_attr_size_new(11 * PANGO_SCALE));
        gtk_label_set_attributes(GTK_LABEL(lbl), attrs);
        pango_attr_list_unref(attrs);
        gtk_container_add(GTK_CONTAINER(r), lbl);
        gtk_container_add(GTK_CONTAINER(g_port_list_box), r);
        count++;
    }
    g_strfreev(lines);

    if (count == 0) {
        GtkWidget *r = gtk_list_box_row_new();
        GtkWidget *e = gtk_label_new("No listening TCP ports found.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(r), e);
        gtk_container_add(GTK_CONTAINER(g_port_list_box), r);
    }
    gtk_widget_show_all(g_port_list_box);
}

static GtkWidget *build_port_monitor_tab(void) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(hdr), "header-box");
    GtkWidget *ht = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(ht, TRUE);
    GtkWidget *t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span font_weight=\"bold\" size=\"12000\">Port Monitor</span>");
    gtk_widget_set_halign(t, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "title-label");
    gtk_box_pack_start(GTK_BOX(ht), t, FALSE, FALSE, 0);
    GtkWidget *sub = gtk_label_new("Active TCP listening ports (ss -tlnp)");
    gtk_widget_set_halign(sub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(ht), sub, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hdr), ht, TRUE, TRUE, 0);
    GtkWidget *rescan = gtk_button_new_with_label("Refresh");
    gtk_style_context_add_class(gtk_widget_get_style_context(rescan), "edit-button");
    g_signal_connect_swapped(rescan, "clicked", G_CALLBACK(refresh_port_list), NULL);
    gtk_box_pack_end(GTK_BOX(hdr), rescan, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), hdr, FALSE, FALSE, 0);
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    g_port_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_port_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sc), g_port_list_box);
    gtk_box_pack_start(GTK_BOX(root), sc, TRUE, TRUE, 0);
    refresh_port_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Command Palette (Ctrl+P)
 *  Overlay window with fuzzy search across all tabs
 * ──────────────────────────────────────────────────────────────────────── */

static GtkWidget *g_palette_window    = NULL;
static GtkWidget *g_palette_search    = NULL;
static GtkWidget *g_palette_listbox   = NULL;
static int        g_palette_selected  = 0;

static void
on_palette_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
    (void)box; (void)data;
    if (row == NULL || g_notebook == NULL) return;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "tab_idx"));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), idx);
    gtk_widget_hide(g_palette_window);
}

static void
palette_filter(const char *query)
{
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_palette_listbox));
    for (GList *l = kids; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    int match = 0;
    for (int i = 0; g_home_cards[i].icon != NULL; i++) {
        const char *title = g_home_cards[i].title;
        const char *desc  = g_home_cards[i].desc;
        gboolean show = TRUE;

        if (query && *query) {
            show = FALSE;
            gchar *query_lower = g_utf8_strdown(query, -1);
            gchar *title_lower = g_utf8_strdown(title, -1);
            gchar *desc_lower  = g_utf8_strdown(desc, -1);
            if (strstr(title_lower, query_lower) ||
                strstr(desc_lower, query_lower))
                show = TRUE;
            g_free(query_lower);
            g_free(title_lower);
            g_free(desc_lower);
        }

        if (!show) continue;

        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "tab_idx",
            GINT_TO_POINTER(g_home_cards[i].tab_index));

        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(hbox, 12);
        gtk_widget_set_margin_end(hbox, 12);
        gtk_widget_set_margin_top(hbox, 6);
        gtk_widget_set_margin_bottom(hbox, 6);

        GtkWidget *icon = gtk_label_new(g_home_cards[i].icon);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(icon), "home-card-icon");
        gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(vbox, TRUE);

        GtkWidget *t = gtk_label_new(title);
        gtk_widget_set_halign(t, GTK_ALIGN_START);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(t), "account-name");
        gtk_box_pack_start(GTK_BOX(vbox), t, FALSE, FALSE, 0);

        GtkWidget *d = gtk_label_new(desc);
        gtk_widget_set_halign(d, GTK_ALIGN_START);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(d), "account-meta");
        gtk_box_pack_start(GTK_BOX(vbox), d, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(row), hbox);
        gtk_container_add(GTK_CONTAINER(g_palette_listbox), row);
        match++;
    }

    if (match == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *e = gtk_label_new("No matching tabs.");
        gtk_style_context_add_class(gtk_widget_get_style_context(e), "empty-label");
        gtk_container_add(GTK_CONTAINER(row), e);
        gtk_container_add(GTK_CONTAINER(g_palette_listbox), row);
    }
    gtk_widget_show_all(g_palette_listbox);
    g_palette_selected = 0;
}

static void
on_palette_search_changed(GtkSearchEntry *entry, gpointer data)
{
    (void)data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    palette_filter(text);
}

static gboolean
on_palette_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget; (void)data;
    int count = 0;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_palette_listbox));
    count = g_list_length(kids);
    g_list_free(kids);

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(g_palette_window);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Down) {
        g_palette_selected = (g_palette_selected + 1) % MAX(count, 1);
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(g_palette_listbox), g_palette_selected);
        if (row) gtk_list_box_select_row(
            GTK_LIST_BOX(g_palette_listbox), row);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Up) {
        g_palette_selected = (g_palette_selected - 1 + count) % MAX(count, 1);
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(g_palette_listbox), g_palette_selected);
        if (row) gtk_list_box_select_row(
            GTK_LIST_BOX(g_palette_listbox), row);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return) {
        GtkListBoxRow *row = gtk_list_box_get_selected_row(
            GTK_LIST_BOX(g_palette_listbox));
        if (row) {
            on_palette_row_activated(GTK_LIST_BOX(g_palette_listbox), row, NULL);
        }
        return TRUE;
    }
    return FALSE;
}

static void
palette_init(void)
{
    g_palette_window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_transient_for(GTK_WINDOW(g_palette_window),
        GTK_WINDOW(g_main_window));
    gtk_window_set_modal(GTK_WINDOW(g_palette_window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(g_palette_window), 440, 400);
    gtk_window_set_position(GTK_WINDOW(g_palette_window),
        GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_type_hint(GTK_WINDOW(g_palette_window),
        GDK_WINDOW_TYPE_HINT_DIALOG);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_palette_window), root);

    g_palette_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_palette_search),
        "Search tabs and actions...");
    gtk_widget_set_margin_start(g_palette_search, 8);
    gtk_widget_set_margin_end(g_palette_search, 8);
    gtk_widget_set_margin_top(g_palette_search, 8);
    gtk_widget_set_margin_bottom(g_palette_search, 4);
    g_signal_connect(g_palette_search, "search-changed",
        G_CALLBACK(on_palette_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(root), g_palette_search, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    g_palette_listbox = gtk_list_box_new();
    g_signal_connect(g_palette_listbox, "row-activated",
        G_CALLBACK(on_palette_row_activated), NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), g_palette_listbox);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    GtkWidget *footer = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(footer),
        "<span size=\"x-small\" foreground=\"#86868b\">"
        "↑↓ navigate  ·  Enter select  ·  Esc close</span>");
    gtk_widget_set_margin_start(footer, 8); gtk_widget_set_margin_end(footer, 8);
    gtk_widget_set_margin_top(footer, 4); gtk_widget_set_margin_bottom(footer, 8);
    gtk_widget_set_halign(footer, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 0);

    g_signal_connect(g_palette_window, "key-press-event",
        G_CALLBACK(on_palette_key_press), NULL);

    palette_filter(NULL);
}

static void
palette_show(void)
{
    if (!g_palette_window) palette_init();
    gtk_entry_set_text(GTK_ENTRY(g_palette_search), "");
    palette_filter(NULL);
    gtk_widget_show_all(g_palette_window);
    gtk_widget_grab_focus(g_palette_search);
}

/* ────────────────────────────────────────────────────────────────────────
 *  Config Opener tab (tab 1) — dynamic, SQLite-backed
 * ──────────────────────────────────────────────────────────────────────── */

static void refresh_config_list(void);

/* Add / edit dialog for ConfigEntry. */
typedef struct {
    int   id;        /* 0 = new */
    char *name;
    char *path;
} ConfigForm;

static gboolean
run_config_dialog(GtkWindow *parent, ConfigForm *form)
{
    const char *title = (form->id == 0) ? "Add Config" : "Edit Config";

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title, parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 580, 220);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    install_shortcuts(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    GtkWidget *lab_name = gtk_label_new("Name");
    gtk_widget_set_halign(lab_name, GTK_ALIGN_END);
    GtkWidget *en_name = gtk_entry_new();
    gtk_widget_set_hexpand(en_name, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(en_name), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_name), "e.g. zshrc");
    if (form->name) gtk_entry_set_text(GTK_ENTRY(en_name), form->name);
    gtk_grid_attach(GTK_GRID(grid), lab_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), en_name,  1, 0, 1, 1);

    GtkWidget *lab_path = gtk_label_new("Path");
    gtk_widget_set_halign(lab_path, GTK_ALIGN_END);
    GtkWidget *en_path = gtk_entry_new();
    gtk_widget_set_hexpand(en_path, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(en_path), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_path),
                                   "~/.config/foo/config.json");
    if (form->path) gtk_entry_set_text(GTK_ENTRY(en_path), form->path);
    gtk_grid_attach(GTK_GRID(grid), lab_path, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), en_path,  1, 1, 1, 1);

    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(hint),
        "<span size=\"x-small\" foreground=\"#86868b\">"
        "Path may start with <tt>~</tt> — expanded to $HOME at open time."
        "</span>");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), hint, 1, 2, 1, 1);

    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(en_name);

    gboolean saved = FALSE;
    while (TRUE) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
        if (resp != GTK_RESPONSE_ACCEPT) break;

        const char *name = gtk_entry_get_text(GTK_ENTRY(en_name));
        const char *path = gtk_entry_get_text(GTK_ENTRY(en_path));

        if (name == NULL || *name == 0 ||
            path == NULL || *path == 0) {
            show_error(dialog, "Both Name and Path are required.");
            continue;
        }
        g_free(form->name); form->name = g_strdup(name);
        g_free(form->path); form->path = g_strdup(path);
        saved = TRUE;
        break;
    }
    gtk_widget_destroy(dialog);
    return saved;
}

static void
on_config_open_clicked(GtkButton *button, gpointer user_data)
{
    ConfigEntry *c = (ConfigEntry *)user_data;
    char *full_path = expand_path(c->path);
    if (full_path == NULL) return;

    if (!g_file_test(full_path, G_FILE_TEST_IS_REGULAR)) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "File not found:\n%s\n\n"
            "The editor may create a new file or show an error.",
            full_path);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    launch_editor(GTK_WIDGET(button), full_path);
    g_free(full_path);
}

static void
on_config_edit_clicked(GtkButton *button, gpointer user_data)
{
    ConfigEntry *c = (ConfigEntry *)user_data;
    ConfigForm form = {
        .id   = c->id,
        .name = g_strdup(c->name),
        .path = g_strdup(c->path),
    };
    if (run_config_dialog(GTK_WINDOW(g_main_window), &form)) {
        char *err = NULL;
        if (db_update_config(form.id, form.name, form.path, &err) != 0) {
            show_error(GTK_WIDGET(button),
                "Failed to update config:\n%s", err ? err : "?");
            g_free(err);
        } else {
            set_status("✓ Updated config '%s'", form.name);
            refresh_config_list();
        }
    }
    g_free(form.name);
    g_free(form.path);
}

static void
on_config_delete_clicked(GtkButton *button, gpointer user_data)
{
    ConfigEntry *c = (ConfigEntry *)user_data;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Delete config entry '%s'?\n\n"
        "The file on disk is left untouched.",
        c->name);
    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp != GTK_RESPONSE_YES) return;

    char *err = NULL;
    if (db_delete_config(c->id, &err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to delete config:\n%s", err ? err : "?");
        g_free(err);
        return;
    }
    set_status("Deleted config '%s'", c->name);
    refresh_config_list();
}

static void
on_config_add_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    ConfigForm form = {0};
    if (run_config_dialog(GTK_WINDOW(g_main_window), &form)) {
        char *err = NULL;
        if (db_insert_config(form.name, form.path, &err) != 0) {
            show_error(GTK_WIDGET(button),
                "Failed to insert config:\n%s", err ? err : "?");
            g_free(err);
        } else {
            set_status("✓ Added config '%s'", form.name);
            refresh_config_list();
        }
    }
    g_free(form.name);
    g_free(form.path);
}

static GtkWidget *
create_config_row(ConfigEntry *c)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox,  12);
    gtk_widget_set_margin_end(hbox,    8);
    gtk_widget_set_margin_top(hbox,    6);
    gtk_widget_set_margin_bottom(hbox, 6);

    /* Two-line name + path */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);

    GtkWidget *name = gtk_label_new(c->name);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(name), "account-name");
    gtk_box_pack_start(GTK_BOX(vbox), name, FALSE, FALSE, 0);

    GtkWidget *path_lbl = gtk_label_new(c->path);
    gtk_widget_set_halign(path_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(path_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(path_lbl), "account-meta");
    gtk_box_pack_start(GTK_BOX(vbox), path_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    /* Buttons: Edit · Delete · Open */
    GtkWidget *edit_btn = gtk_button_new_with_label("Edit");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(edit_btn), "edit-button");
    g_signal_connect(edit_btn, "clicked",
                     G_CALLBACK(on_config_edit_clicked), c);
    gtk_box_pack_start(GTK_BOX(hbox), edit_btn, FALSE, FALSE, 0);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(del_btn), "delete-button");
    g_signal_connect(del_btn, "clicked",
                     G_CALLBACK(on_config_delete_clicked), c);
    gtk_box_pack_start(GTK_BOX(hbox), del_btn, FALSE, FALSE, 0);

    GtkWidget *open_btn = gtk_button_new_with_label("Open");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(open_btn), "open-button");
    g_signal_connect(open_btn, "clicked",
                     G_CALLBACK(on_config_open_clicked), c);
    char *expanded = expand_path(c->path);
    if (expanded) {
        gtk_widget_set_tooltip_text(open_btn, expanded);
        g_free(expanded);
    }
    gtk_box_pack_start(GTK_BOX(hbox), open_btn, FALSE, FALSE, 0);

    return hbox;
}

static void
refresh_config_list(void)
{
    if (g_config_list_box == NULL) return;

    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_config_list_box));
    for (GList *l = kids; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    GPtrArray *items = db_list_configs();
    if (items->len == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        GtkWidget *empty = gtk_label_new(
            "No configs yet.  Click \"+ Add Config\" to create one.");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(empty), "empty-label");
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(row), empty);
        gtk_container_add(GTK_CONTAINER(g_config_list_box), row);
    } else {
        for (guint i = 0; i < items->len; i++) {
            ConfigEntry *c = g_ptr_array_index(items, i);
            g_ptr_array_index(items, i) = NULL;       /* row takes ownership */

            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
            GtkWidget *content = create_config_row(c);
            gtk_container_add(GTK_CONTAINER(row), content);
            gtk_container_add(GTK_CONTAINER(g_config_list_box), row);

            g_object_set_data_full(G_OBJECT(row), "config",
                c, (GDestroyNotify)config_entry_free);
        }
    }
    g_ptr_array_free(items, TRUE);
    gtk_widget_show_all(g_config_list_box);
}

static GtkWidget *
build_config_opener_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header with title + Add */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(header), "header-box");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(header_text, TRUE);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span font_weight=\"bold\" size=\"12000\">"
                         "Configuration Files</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(title), "title-label");
    gtk_box_pack_start(GTK_BOX(header_text), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new(
        "Manage config file shortcuts  ·  open them with $EDITOR");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(subtitle), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(header_text), subtitle, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), header_text, TRUE, TRUE, 0);

    GtkWidget *add_btn = gtk_button_new_with_label("+ Add Config");
    gtk_widget_set_valign(add_btn, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(add_btn), "add-button");
    g_signal_connect(add_btn, "clicked",
                     G_CALLBACK(on_config_add_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(header), add_btn, FALSE, FALSE, 0);

    /* Scrolled list */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    g_config_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_config_list_box),
                                    GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), g_config_list_box);

    refresh_config_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Claude Switcher tab (tab 2)
 * ──────────────────────────────────────────────────────────────────────── */

static void refresh_account_list(void);

/* Edit/Add dialog.  Returns TRUE if user saved. */
typedef struct {
    int   id;        /* 0 = new */
    char *name;
    char *api_key;
    char *base_url;
} AccountForm;

static gboolean
run_account_dialog(GtkWindow *parent, AccountForm *form)
{
    const char *title = (form->id == 0) ? "Add Account" : "Edit Account";

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title, parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 260);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    install_shortcuts(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    GtkWidget *lab_name = gtk_label_new("Name");
    gtk_widget_set_halign(lab_name, GTK_ALIGN_END);
    GtkWidget *en_name = gtk_entry_new();
    gtk_widget_set_hexpand(en_name, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(en_name), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_name), "e.g. aerolink-prod");
    if (form->name) gtk_entry_set_text(GTK_ENTRY(en_name), form->name);
    gtk_grid_attach(GTK_GRID(grid), lab_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), en_name,  1, 0, 1, 1);

    GtkWidget *lab_key = gtk_label_new("API Key");
    gtk_widget_set_halign(lab_key, GTK_ALIGN_END);
    GtkWidget *en_key = gtk_entry_new();
    gtk_widget_set_hexpand(en_key, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(en_key), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(en_key),
                                    "paste with Ctrl+V");
    if (form->api_key) gtk_entry_set_text(GTK_ENTRY(en_key), form->api_key);
    gtk_grid_attach(GTK_GRID(grid), lab_key, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), en_key,  1, 1, 1, 1);

    GtkWidget *lab_url = gtk_label_new("Base URL");
    gtk_widget_set_halign(lab_url, GTK_ALIGN_END);
    GtkWidget *en_url = gtk_entry_new();
    gtk_widget_set_hexpand(en_url, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(en_url), TRUE);
    if (form->base_url) gtk_entry_set_text(GTK_ENTRY(en_url), form->base_url);
    else                gtk_entry_set_placeholder_text(GTK_ENTRY(en_url),
                                "https://api.anthropic.com/");
    gtk_grid_attach(GTK_GRID(grid), lab_url, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), en_url,  1, 2, 1, 1);

    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(hint),
        "<span size=\"x-small\" foreground=\"#86868b\">"
        "Ctrl+C / V / X / A work in these fields.  Press Enter to save."
        "</span>");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), hint, 1, 3, 1, 1);

    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(en_name);

    gboolean saved = FALSE;
    while (TRUE) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
        if (resp != GTK_RESPONSE_ACCEPT) break;

        const char *name = gtk_entry_get_text(GTK_ENTRY(en_name));
        const char *key  = gtk_entry_get_text(GTK_ENTRY(en_key));
        const char *url  = gtk_entry_get_text(GTK_ENTRY(en_url));

        if (name == NULL || *name == 0 ||
            key  == NULL || *key  == 0 ||
            url  == NULL || *url  == 0) {
            show_error(dialog,
                "All fields are required: Name, API Key, Base URL.");
            continue;
        }

        g_free(form->name);     form->name     = g_strdup(name);
        g_free(form->api_key);  form->api_key  = g_strdup(key);
        g_free(form->base_url); form->base_url = g_strdup(url);
        saved = TRUE;
        break;
    }
    gtk_widget_destroy(dialog);
    return saved;
}

/* ── Account row callbacks ── */

static void
on_activate_clicked(GtkButton *button, gpointer user_data)
{
    Account *a = (Account *)user_data;

    /* 1. backup current */
    char *err = NULL;
    if (backup_settings_json(&err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to back up existing settings.json:\n%s",
            err ? err : "?");
        g_free(err);
        return;
    }
    g_free(err); err = NULL;

    /* 2. write new */
    if (write_settings_json(a, &err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to write ~/.claude/settings.json:\n%s",
            err ? err : "?");
        g_free(err);
        return;
    }
    g_free(err); err = NULL;

    /* 3. update DB flag */
    if (db_set_active(a->id, &err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to mark active in DB:\n%s",
            err ? err : "?");
        g_free(err);
        return;
    }

    set_status("✓ Activated '%s'  ·  ~/.claude/settings.json updated (backup: settings.json.bak)",
               a->name);
    refresh_account_list();
}

static void
on_edit_clicked(GtkButton *button, gpointer user_data)
{
    Account *a = (Account *)user_data;

    AccountForm form = {
        .id       = a->id,
        .name     = g_strdup(a->name),
        .api_key  = g_strdup(a->api_key),
        .base_url = g_strdup(a->base_url),
    };

    if (run_account_dialog(GTK_WINDOW(g_main_window), &form)) {
        char *err = NULL;
        if (db_update(form.id, form.name, form.api_key, form.base_url, &err) != 0) {
            show_error(GTK_WIDGET(button),
                "Failed to update account:\n%s", err ? err : "?");
            g_free(err);
        } else {
            set_status("✓ Updated account '%s'", form.name);

            /* If this is the currently active account, also refresh
             * ~/.claude/settings.json so the live file matches the new
             * credentials. */
            if (a->is_active) {
                Account tmp = {
                    .id = form.id,
                    .name = form.name,
                    .api_key = form.api_key,
                    .base_url = form.base_url,
                    .is_active = 1,
                };
                backup_settings_json(NULL);
                write_settings_json(&tmp, NULL);
                set_status("✓ Updated '%s' (re-synced ~/.claude/settings.json)",
                           form.name);
            }
            refresh_account_list();
        }
    }

    g_free(form.name);
    g_free(form.api_key);
    g_free(form.base_url);
}

static void
on_delete_clicked(GtkButton *button, gpointer user_data)
{
    Account *a = (Account *)user_data;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Delete account '%s'?\n\n"
        "This only removes the entry from the database. "
        "~/.claude/settings.json is left untouched.",
        a->name);
    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp != GTK_RESPONSE_YES) return;

    char *err = NULL;
    if (db_delete(a->id, &err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to delete account:\n%s", err ? err : "?");
        g_free(err);
        return;
    }
    set_status("Deleted account '%s'", a->name);
    refresh_account_list();
}

static void
on_open_account_clicked(GtkButton *button, gpointer user_data)
{
    Account *a = (Account *)user_data;
    char *path = write_preview_json(a);
    launch_editor(GTK_WIDGET(button), path);
    g_free(path);
}

/* Build one row in the accounts list. */
static GtkWidget *
create_account_row(Account *a)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox,  12);
    gtk_widget_set_margin_end(hbox,    8);
    gtk_widget_set_margin_top(hbox,    8);
    gtk_widget_set_margin_bottom(hbox, 8);

    /* Active indicator */
    GtkWidget *dot = gtk_drawing_area_new();
    gtk_widget_set_size_request(dot, 14, 14);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(dot),
        a->is_active ? "dot-active" : "dot-inactive");
    gtk_widget_set_tooltip_text(dot,
        a->is_active ? "Active — currently written to ~/.claude/settings.json"
                     : "Inactive");
    gtk_box_pack_start(GTK_BOX(hbox), dot, FALSE, FALSE, 0);

    /* Two-line name + meta */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);

    GtkWidget *name = gtk_label_new(a->name);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(name), "account-name");
    gtk_box_pack_start(GTK_BOX(vbox), name, FALSE, FALSE, 0);

    char *masked = mask_api_key(a->api_key);
    char *meta   = g_strdup_printf("%s  ·  %s", masked, a->base_url);
    g_free(masked);
    GtkWidget *meta_lbl = gtk_label_new(meta);
    g_free(meta);
    gtk_widget_set_halign(meta_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(meta_lbl), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(meta_lbl), "account-meta");
    gtk_box_pack_start(GTK_BOX(vbox), meta_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    /* Buttons.  Order: Activate · Edit · Delete · Open */
    GtkWidget *activate_btn = gtk_button_new_with_label(
        a->is_active ? "Active" : "Activate");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(activate_btn), "activate-button");
    gtk_widget_set_sensitive(activate_btn, !a->is_active);
    g_signal_connect(activate_btn, "clicked",
                     G_CALLBACK(on_activate_clicked), a);
    gtk_box_pack_start(GTK_BOX(hbox), activate_btn, FALSE, FALSE, 0);

    GtkWidget *edit_btn = gtk_button_new_with_label("Edit");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(edit_btn), "edit-button");
    g_signal_connect(edit_btn, "clicked",
                     G_CALLBACK(on_edit_clicked), a);
    gtk_box_pack_start(GTK_BOX(hbox), edit_btn, FALSE, FALSE, 0);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(del_btn), "delete-button");
    g_signal_connect(del_btn, "clicked",
                     G_CALLBACK(on_delete_clicked), a);
    gtk_box_pack_start(GTK_BOX(hbox), del_btn, FALSE, FALSE, 0);

    GtkWidget *open_btn = gtk_button_new_with_label("Open");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(open_btn), "open-button");
    gtk_widget_set_tooltip_text(open_btn,
        "Write this account's settings.json preview to disk and open it in "
        "the default editor (does not activate).");
    g_signal_connect(open_btn, "clicked",
                     G_CALLBACK(on_open_account_clicked), a);
    gtk_box_pack_start(GTK_BOX(hbox), open_btn, FALSE, FALSE, 0);

    return hbox;
}

/* Rebuild the listbox from the DB. */
static void
refresh_account_list(void)
{
    if (g_account_list_box == NULL) return;

    /* clear */
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_account_list_box));
    for (GList *l = kids; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    GPtrArray *accounts = db_list_accounts();

    if (accounts->len == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        GtkWidget *empty = gtk_label_new(
            "No accounts yet.  Click \"+ Add Account\" to create one.");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(empty), "empty-label");
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(row), empty);
        gtk_container_add(GTK_CONTAINER(g_account_list_box), row);
    } else {
        for (guint i = 0; i < accounts->len; i++) {
            Account *a = g_ptr_array_index(accounts, i);

            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);

            /* Detach Account so callbacks own it for the row's lifetime. */
            g_ptr_array_index(accounts, i) = NULL;

            GtkWidget *content = create_account_row(a);
            gtk_container_add(GTK_CONTAINER(row), content);
            gtk_container_add(GTK_CONTAINER(g_account_list_box), row);

            /* Free Account when its row is destroyed. */
            g_object_set_data_full(G_OBJECT(row), "account",
                a, (GDestroyNotify)account_free);
        }
    }

    g_ptr_array_free(accounts, TRUE);
    gtk_widget_show_all(g_account_list_box);
}

/* If an account is currently active, rewrite ~/.claude/settings.json so the
 * live file reflects the latest plugin list.  Silent (status bar only). */
static void
resync_active_settings(void)
{
    Account *a = db_get_active_account();
    if (a == NULL) return;
    backup_settings_json(NULL);
    write_settings_json(a, NULL);
    account_free(a);
}

/* ── Plugins dialog ───────────────────────────────────────────────────── */

static void refresh_plugins_list(void);

static void
on_plugin_toggle(GtkToggleButton *btn, gpointer user_data)
{
    const char *name = (const char *)user_data;
    db_set_plugin_enabled(name,
        gtk_toggle_button_get_active(btn) ? 1 : 0, NULL);
}

static void
on_plugin_delete(GtkButton *button, gpointer user_data)
{
    (void)button;
    const char *name = (const char *)user_data;
    db_delete_plugin(name, NULL);
    refresh_plugins_list();
}

static void
on_plugin_add(GtkButton *button, gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);
    const char *text = gtk_entry_get_text(entry);
    if (text == NULL || *text == 0) return;

    char *err = NULL;
    if (db_insert_plugin(text, 1, &err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to add plugin '%s':\n%s",
            text, err ? err : "?");
        g_free(err);
        return;
    }
    gtk_entry_set_text(entry, "");
    refresh_plugins_list();
}

static GtkWidget *
create_plugin_row(Plugin *p)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox,   8);
    gtk_widget_set_margin_top(hbox,    6);
    gtk_widget_set_margin_bottom(hbox, 6);

    GtkWidget *check = gtk_check_button_new();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), p->enabled);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(on_plugin_toggle), p->name);
    gtk_box_pack_start(GTK_BOX(hbox), check, FALSE, FALSE, 0);

    GtkWidget *name = gtk_label_new(p->name);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_widget_set_hexpand(name, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(name), TRUE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(name), "account-name");
    gtk_box_pack_start(GTK_BOX(hbox), name, TRUE, TRUE, 0);

    GtkWidget *del = gtk_button_new_with_label("Delete");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(del), "delete-button");
    g_signal_connect(del, "clicked", G_CALLBACK(on_plugin_delete), p->name);
    gtk_box_pack_start(GTK_BOX(hbox), del, FALSE, FALSE, 0);

    return hbox;
}

static void
refresh_plugins_list(void)
{
    if (g_plugins_list_box == NULL) return;

    GList *kids = gtk_container_get_children(GTK_CONTAINER(g_plugins_list_box));
    for (GList *l = kids; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    GPtrArray *plugins = db_list_plugins();
    if (plugins->len == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        GtkWidget *empty = gtk_label_new(
            "No plugins.  enabledPlugins will be written as { }.");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(empty), "empty-label");
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(row), empty);
        gtk_container_add(GTK_CONTAINER(g_plugins_list_box), row);
    } else {
        for (guint i = 0; i < plugins->len; i++) {
            Plugin *p = g_ptr_array_index(plugins, i);
            /* Detach so the row owns the Plugin. */
            g_ptr_array_index(plugins, i) = NULL;

            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
            GtkWidget *content = create_plugin_row(p);
            gtk_container_add(GTK_CONTAINER(row), content);
            gtk_container_add(GTK_CONTAINER(g_plugins_list_box), row);

            g_object_set_data_full(G_OBJECT(row), "plugin",
                p, (GDestroyNotify)plugin_free);
        }
    }
    g_ptr_array_free(plugins, TRUE);
    gtk_widget_show_all(g_plugins_list_box);
}

static void
on_plugins_clicked(GtkButton *button, gpointer user_data)
{
    (void)button; (void)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Manage Plugins",
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 440);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    install_shortcuts(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    /* Header text */
    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(hint),
        "<span size=\"small\" foreground=\"#86868b\">"
        "These plugins are written to <tt>enabledPlugins</tt> for every "
        "account activation and on Reset.\n"
        "Toggle to enable / disable. Plugin name format: "
        "<tt>name@source</tt>."
        "</span>");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(hint, 8);
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);

    /* Add plugin row */
    GtkWidget *add_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *new_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(new_entry),
        "plugin-name@source (e.g. rust-lsp@claude-plugins-official)");
    gtk_widget_set_hexpand(new_entry, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(new_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(add_row), new_entry, TRUE, TRUE, 0);

    GtkWidget *add_btn = gtk_button_new_with_label("+ Add Plugin");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(add_btn), "add-button");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_plugin_add), new_entry);
    g_signal_connect_swapped(new_entry, "activate",
        G_CALLBACK(gtk_button_clicked), add_btn);
    gtk_box_pack_start(GTK_BOX(add_row), add_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), add_row, FALSE, FALSE, 0);

    /* List */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_widget_set_margin_top(scrolled, 10);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);

    g_plugins_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_plugins_list_box),
                                    GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), g_plugins_list_box);

    refresh_plugins_list();
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(new_entry);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_plugins_list_box = NULL;

    /* Apply plugin changes to the live settings.json if there is an active
     * account.  Reset is unaffected — user clicks Reset explicitly. */
    Account *active = db_get_active_account();
    if (active != NULL) {
        resync_active_settings();
        set_status("✓ Plugins saved  ·  re-synced ~/.claude/settings.json for '%s'",
                   active->name);
        account_free(active);
        refresh_account_list();
    } else {
        set_status("✓ Plugins saved");
    }
}

static void
on_reset_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;

    GtkWidget *confirm = gtk_message_dialog_new(
        GTK_WINDOW(g_main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Reset ~/.claude/settings.json to default?\n\n"
        "This removes the API key / base URL block and rewrites the file with "
        "the default Claude config (model, plugins, theme).\n\n"
        "Current settings.json is backed up to settings.json.bak.");
    gint resp = gtk_dialog_run(GTK_DIALOG(confirm));
    gtk_widget_destroy(confirm);
    if (resp != GTK_RESPONSE_YES) return;

    char *err = NULL;
    if (backup_settings_json(&err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to back up existing settings.json:\n%s",
            err ? err : "?");
        g_free(err);
        return;
    }
    g_free(err); err = NULL;

    if (write_reset_settings_json(&err) != 0) {
        show_error(GTK_WIDGET(button),
            "Failed to write ~/.claude/settings.json:\n%s",
            err ? err : "?");
        g_free(err);
        return;
    }

    /* No account corresponds to the reset config — clear active flags. */
    sqlite3_exec(g_db, "UPDATE accounts SET is_active=0;", NULL, NULL, NULL);

    set_status("✓ Reset ~/.claude/settings.json to default  ·  backup: settings.json.bak");
    refresh_account_list();
}

static void
on_add_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    AccountForm form = {0};
    form.base_url = g_strdup("https://api.anthropic.com/");

    if (run_account_dialog(GTK_WINDOW(g_main_window), &form)) {
        char *err = NULL;
        if (db_insert(form.name, form.api_key, form.base_url, &err) != 0) {
            show_error(GTK_WIDGET(button),
                "Failed to insert account:\n%s", err ? err : "?");
            g_free(err);
        } else {
            set_status("✓ Added account '%s'", form.name);
            refresh_account_list();
        }
    }
    g_free(form.name);
    g_free(form.api_key);
    g_free(form.base_url);
}

static GtkWidget *
build_claude_switcher_tab(void)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header with title + Add button */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(header), "header-box");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(header_text, TRUE);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span font_weight=\"bold\" size=\"12000\">"
                         "Claude Switcher</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(title), "title-label");
    gtk_box_pack_start(GTK_BOX(header_text), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new(
        "Manage Claude API accounts  ·  green dot = active in ~/.claude/settings.json");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(subtitle), "subtitle-label");
    gtk_box_pack_start(GTK_BOX(header_text), subtitle, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), header_text, TRUE, TRUE, 0);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_valign(btn_box, GTK_ALIGN_CENTER);

    GtkWidget *plugins_btn = gtk_button_new_with_label("Plugins");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(plugins_btn), "plugins-button");
    gtk_widget_set_tooltip_text(plugins_btn,
        "Manage the enabledPlugins list applied to every account "
        "activation and to Reset.");
    g_signal_connect(plugins_btn, "clicked",
                     G_CALLBACK(on_plugins_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), plugins_btn, FALSE, FALSE, 0);

    GtkWidget *reset_btn = gtk_button_new_with_label("Reset");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(reset_btn), "reset-button");
    gtk_widget_set_tooltip_text(reset_btn,
        "Reset ~/.claude/settings.json to the default config "
        "(no API key block). Current file is backed up to .bak.");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), reset_btn, FALSE, FALSE, 0);

    GtkWidget *add_btn = gtk_button_new_with_label("+ Add Account");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(add_btn), "add-button");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), add_btn, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(header), btn_box, FALSE, FALSE, 0);

    /* Scrolled list */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

    g_account_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_account_list_box),
                                    GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), g_account_list_box);

    refresh_account_list();
    return root;
}

/* ────────────────────────────────────────────────────────────────────────
 *  Apply CSS / lifecycle
 * ──────────────────────────────────────────────────────────────────────── */

/* Load CSS_VARS_<mode> + CSS_BODY into our application-level provider.  Called
 * once on startup and again every time the user toggles the theme. */
static void
apply_theme(gboolean dark)
{
    g_dark_mode = dark;

    /* Hint native widgets (dialogs, scrollbars, default entries) to follow
     * the dark/light preference so they blend with our custom CSS. */
    GtkSettings *settings = gtk_settings_get_default();
    if (settings != NULL) {
        g_object_set(settings,
                     "gtk-application-prefer-dark-theme", dark,
                     NULL);
    }

    const char *vars     = dark ? CSS_VARS_DARK : CSS_VARS_LIGHT;
    char       *combined = g_strconcat(vars, CSS_BODY, NULL);

    if (g_css_provider == NULL) {
        g_css_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(g_css_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    GError *error = NULL;
    gtk_css_provider_load_from_data(g_css_provider, combined, -1, &error);
    if (error != NULL) {
        g_warning("CSS load error: %s", error->message);
        g_error_free(error);
    }
    g_free(combined);
}

static void
on_theme_toggled(GtkButton *button, gpointer user_data)
{
    (void)button; (void)user_data;

    gboolean next = !g_dark_mode;
    apply_theme(next);
    db_set_setting("theme", next ? "dark" : "light");

    if (g_theme_btn != NULL) {
        gtk_button_set_label(GTK_BUTTON(g_theme_btn),
                             next ? "☀ Light" : "🌙 Dark");
        gtk_widget_set_tooltip_text(g_theme_btn,
            next ? "Switch to light mode" : "Switch to dark mode");
    }
    set_status(next ? "Dark mode" : "Light mode");
}

static void
on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
    gtk_main_quit();
}

/* ────────────────────────────────────────────────────────────────────────
 *  Entry point
 * ──────────────────────────────────────────────────────────────────────── */

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    signal(SIGCHLD, SIG_IGN);
    setup_app_icon();

    if (db_init() != 0) {
        fprintf(stderr,
            "[config-opener] Could not open SQLite DB. "
            "Claude Switcher disabled.\n");
    }

    /* Load saved theme preference; default to light. */
    char *saved_theme = (g_db != NULL) ? db_get_setting("theme") : NULL;
    gboolean dark = (saved_theme != NULL && g_strcmp0(saved_theme, "dark") == 0);
    g_free(saved_theme);
    apply_theme(dark);

    /* Main window */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_main_window = window;
    gtk_window_set_title(GTK_WINDOW(window),
                         "Opentool Desktop");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 460);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    install_shortcuts(window);

    /* Root vbox */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    /* Notebook (tabs) */
    GtkWidget *notebook = gtk_notebook_new();
    g_notebook = notebook;
    gtk_box_pack_start(GTK_BOX(root), notebook, TRUE, TRUE, 0);

    /* Tab index must match g_home_cards[].tab_index */
    GtkWidget *tab_home = build_home_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_home,
        gtk_label_new("Home"));

    GtkWidget *tab_opencode = build_opencode_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_opencode,
        gtk_label_new("OpenCode"));

    GtkWidget *tab_claude = build_claude_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_claude,
        gtk_label_new("Claude"));

    GtkWidget *tab_ssh = build_ssh_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_ssh,
        gtk_label_new("SSH"));

    GtkWidget *tab_term = build_terminal_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_term,
        gtk_label_new("Terminal"));

    GtkWidget *tab_chat = build_chat_web_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_chat,
        gtk_label_new("Chat Web"));

    GtkWidget *tab_kimchi = build_kimchi_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_kimchi,
        gtk_label_new("Kimchi"));

    GtkWidget *tab_mimo = build_mimo_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_mimo,
        gtk_label_new("Mimo"));

    GtkWidget *tab_freebuff = build_freebuff_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_freebuff,
        gtk_label_new("Freebuff"));

    GtkWidget *tab_ports = build_port_monitor_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_ports,
        gtk_label_new("Port Monitor"));

    GtkWidget *tab1 = build_config_opener_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab1,
        gtk_label_new("Config Opener"));

    GtkWidget *tab2 = build_claude_switcher_tab();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab2,
        gtk_label_new("Claude Switcher"));

    /* Theme toggle — placed at the right end of the tab bar so it's
     * accessible from any tab. */
    g_theme_btn = gtk_button_new_with_label(
        g_dark_mode ? "☀ Light" : "🌙 Dark");
    gtk_widget_set_tooltip_text(g_theme_btn,
        g_dark_mode ? "Switch to light mode" : "Switch to dark mode");
    gtk_widget_set_valign(g_theme_btn, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(g_theme_btn), "theme-button");
    g_signal_connect(g_theme_btn, "clicked",
                     G_CALLBACK(on_theme_toggled), NULL);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(notebook),
                                   g_theme_btn, GTK_PACK_END);
    gtk_widget_show(g_theme_btn);   /* action widgets are not in show_all */

    /* Status bar */
    g_status_label = gtk_label_new("Ready");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(g_status_label), "status-bar");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), g_status_label, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}

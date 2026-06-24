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
    {"⚙️", "Config Opener",   "Edit config files",           1},
    {"🔄", "Claude Switcher", "Switch API accounts",         2},
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

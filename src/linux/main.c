/*
 * Native Linux tray helper – JSON-lines stdin/stdout protocol.
 * Uses GTK3 + libayatana-appindicator3 for StatusNotifierItem support.
 * Build:
 *   gcc -O2 main.c cJSON.c $(pkg-config --cflags --libs gtk+-3.0 ayatana-appindicator3-0.1) -lpthread -o tray
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <libayatana-appindicator/app-indicator.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "cJSON.h"

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */
static AppIndicator    *gIndicator;
static GtkWidget       *gMenu;
static pthread_mutex_t  gOutputLock = PTHREAD_MUTEX_INITIALIZER;
static char            *gIconDir;
static int              gIconSeq;
static gboolean         gBuildingMenu;

/* -----------------------------------------------------------------------
 * JSON output
 * ----------------------------------------------------------------------- */
static void emit(const char *method, cJSON *params) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "method", method);
    if (params) cJSON_AddItemToObject(msg, "params", params);
    char *str = cJSON_PrintUnformatted(msg);
    if (str) {
        pthread_mutex_lock(&gOutputLock);
        fputs(str, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        pthread_mutex_unlock(&gOutputLock);
        free(str);
    }
    cJSON_Delete(msg);
}

/* -----------------------------------------------------------------------
 * Base64 decode
 * ----------------------------------------------------------------------- */
static unsigned char b64_rev[256];
static void initB64(void) {
    memset(b64_rev, 0x40, 256);
    const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) b64_rev[(unsigned char)t[i]] = i;
}

static unsigned char *base64Decode(const char *src, size_t *outLen) {
    size_t len = strlen(src);
    if (len % 4 != 0) return NULL;
    size_t dLen = (len / 4) * 3;
    if (src[len-1] == '=') dLen--;
    if (len > 1 && src[len-2] == '=') dLen--;
    unsigned char *out = malloc(dLen);
    for (size_t i = 0, j = 0; i < len; ) {
        unsigned a = b64_rev[(unsigned char)src[i++]];
        unsigned b = b64_rev[(unsigned char)src[i++]];
        unsigned c = b64_rev[(unsigned char)src[i++]];
        unsigned d = b64_rev[(unsigned char)src[i++]];
        unsigned triple = (a << 18) | (b << 12) | (c << 6) | d;
        if (j < dLen) out[j++] = (triple >> 16) & 0xFF;
        if (j < dLen) out[j++] = (triple >> 8) & 0xFF;
        if (j < dLen) out[j++] = triple & 0xFF;
    }
    *outLen = dLen;
    return out;
}

/* -----------------------------------------------------------------------
 * Default icon: 22x22 green circle (#2ead33)
 * ----------------------------------------------------------------------- */
static void writeDefaultIcon(void) {
    int sz = 22;
    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, sz, sz);
    gdk_pixbuf_fill(pb, 0x00000000);
    guchar *px = gdk_pixbuf_get_pixels(pb);
    int stride = gdk_pixbuf_get_rowstride(pb);
    float half = sz / 2.0f, r2 = half * half;
    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++) {
            float dx = x - half + 0.5f, dy = y - half + 0.5f;
            if (dx*dx + dy*dy <= r2) {
                guchar *p = px + y * stride + x * 4;
                p[0] = 0x2e; p[1] = 0xad; p[2] = 0x33; p[3] = 0xff;
            }
        }
    char *path = g_build_filename(gIconDir, "trayjs-default.png", NULL);
    gdk_pixbuf_save(pb, path, "png", NULL, NULL);
    g_object_unref(pb);
    g_free(path);
}

/* -----------------------------------------------------------------------
 * Menu
 * ----------------------------------------------------------------------- */
static void onActivate(GtkMenuItem *item, gpointer data) {
    if (gBuildingMenu) return;
    const char *id = g_object_get_data(G_OBJECT(item), "trayjs-id");
    if (id && *id) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "id", id);
        emit("clicked", p);
    }
}

static void onMenuShow(GtkWidget *w, gpointer d) {
    emit("menuRequested", NULL);
}

static void buildMenuItems(GtkMenuShell *shell, cJSON *items) {
    int n = cJSON_GetArraySize(items);
    for (int i = 0; i < n; i++) {
        cJSON *cfg = cJSON_GetArrayItem(items, i);
        if (cJSON_IsTrue(cJSON_GetObjectItem(cfg, "separator"))) {
            gtk_menu_shell_append(shell, gtk_separator_menu_item_new());
            continue;
        }
        const char *title = cJSON_GetStringValue(cJSON_GetObjectItem(cfg, "title")) ?: "";
        const char *itemId = cJSON_GetStringValue(cJSON_GetObjectItem(cfg, "id")) ?: "";
        cJSON *jChildren = cJSON_GetObjectItem(cfg, "items");

        GtkWidget *mi;
        if (cJSON_IsTrue(cJSON_GetObjectItem(cfg, "checked"))) {
            mi = gtk_check_menu_item_new_with_label(title);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), TRUE);
        } else {
            mi = gtk_menu_item_new_with_label(title);
        }

        g_object_set_data_full(G_OBJECT(mi), "trayjs-id", g_strdup(itemId), g_free);
        if (cJSON_IsFalse(cJSON_GetObjectItem(cfg, "enabled")))
            gtk_widget_set_sensitive(mi, FALSE);

        if (cJSON_IsArray(jChildren) && cJSON_GetArraySize(jChildren) > 0) {
            GtkWidget *sub = gtk_menu_new();
            buildMenuItems(GTK_MENU_SHELL(sub), jChildren);
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), sub);
        } else {
            g_signal_connect(mi, "activate", G_CALLBACK(onActivate), NULL);
        }
        gtk_menu_shell_append(shell, mi);
    }
}

/* -----------------------------------------------------------------------
 * Command handlers (called on GTK main thread via g_idle_add)
 * ----------------------------------------------------------------------- */
static gboolean processCmd(gpointer data) {
    cJSON *m = (cJSON *)data;
    const char *meth = cJSON_GetStringValue(cJSON_GetObjectItem(m, "method"));
    cJSON *p = cJSON_GetObjectItem(m, "params");
    if (!meth) { cJSON_Delete(m); return G_SOURCE_REMOVE; }

    if (!strcmp(meth, "setMenu")) {
        gBuildingMenu = TRUE;
        GtkWidget *newMenu = gtk_menu_new();
        cJSON *items = cJSON_GetObjectItem(p, "items");
        if (items) buildMenuItems(GTK_MENU_SHELL(newMenu), items);
        /* dbusmenu asserts if the menu has no children */
        if (!gtk_container_get_children(GTK_CONTAINER(newMenu))) {
            GtkWidget *ph = gtk_menu_item_new_with_label("");
            gtk_widget_set_sensitive(ph, FALSE);
            gtk_menu_shell_append(GTK_MENU_SHELL(newMenu), ph);
        }
        gtk_widget_show_all(newMenu);
        g_signal_connect(newMenu, "show", G_CALLBACK(onMenuShow), NULL);
        gtk_widget_destroy(gMenu);
        gMenu = newMenu;
        app_indicator_set_menu(gIndicator, GTK_MENU(gMenu));
        gBuildingMenu = FALSE;
    } else if (!strcmp(meth, "setIcon")) {
        const char *b64 = cJSON_GetStringValue(cJSON_GetObjectItem(p, "base64"));
        if (b64) {
            size_t len;
            unsigned char *d = base64Decode(b64, &len);
            if (d) {
                char name[64];
                snprintf(name, sizeof(name), "trayjs-icon-%d.png", ++gIconSeq);
                char *path = g_build_filename(gIconDir, name, NULL);
                g_file_set_contents(path, (const char *)d, len, NULL);
                name[strlen(name) - 4] = '\0'; /* strip .png for icon name */
                app_indicator_set_icon_full(gIndicator, name, "icon");
                g_free(path);
                free(d);
            }
        }
    } else if (!strcmp(meth, "setTooltip")) {
        const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(p, "text"));
        if (text) app_indicator_set_title(gIndicator, text);
    } else if (!strcmp(meth, "quit")) {
        app_indicator_set_status(gIndicator, APP_INDICATOR_STATUS_PASSIVE);
        cJSON_Delete(m);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    cJSON_Delete(m);
    return G_SOURCE_REMOVE;
}

/* -----------------------------------------------------------------------
 * Stdin reader thread
 * ----------------------------------------------------------------------- */
static void *stdinReader(void *arg) {
    char *line = NULL; size_t cap = 0; ssize_t len;
    while ((len = getline(&line, &cap, stdin)) > 0) {
        if (line[len-1] == '\n') line[--len] = '\0';
        if (len == 0) continue;
        cJSON *m = cJSON_Parse(line);
        if (m) g_idle_add(processCmd, m);
    }
    free(line);
    g_idle_add((GSourceFunc)gtk_main_quit, NULL);
    return NULL;
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    initB64();

    /* Create temp icon directory */
    char tmpl[] = "/tmp/trayjs-icons-XXXXXX";
    gIconDir = g_strdup(mkdtemp(tmpl));

    /* Parse args */
    const char *iconPath = NULL, *tooltip = "Tray";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--icon") && i+1 < argc) iconPath = argv[++i];
        if (!strcmp(argv[i], "--tooltip") && i+1 < argc) tooltip = argv[++i];
    }

    /* Create indicator */
    gIndicator = app_indicator_new("trayjs", "trayjs-default",
                                    APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_icon_theme_path(gIndicator, gIconDir);
    app_indicator_set_status(gIndicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(gIndicator, tooltip);

    /* Set icon */
    if (iconPath) {
        gchar *data; gsize len;
        if (g_file_get_contents(iconPath, &data, &len, NULL)) {
            char *dest = g_build_filename(gIconDir, "trayjs-custom.png", NULL);
            g_file_set_contents(dest, data, len, NULL);
            g_free(data); g_free(dest);
            app_indicator_set_icon_full(gIndicator, "trayjs-custom", "icon");
        }
    } else {
        writeDefaultIcon();
    }

    /* Create menu – must contain at least one item or libdbusmenu
       will reject it with assertion failures. */
    gMenu = gtk_menu_new();
    GtkWidget *ph = gtk_menu_item_new_with_label("");
    gtk_widget_set_sensitive(ph, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(gMenu), ph);
    gtk_widget_show_all(gMenu);
    g_signal_connect(gMenu, "show", G_CALLBACK(onMenuShow), NULL);
    app_indicator_set_menu(gIndicator, GTK_MENU(gMenu));

    emit("ready", NULL);

    /* Start stdin reader */
    pthread_t tid;
    pthread_create(&tid, NULL, stdinReader, NULL);

    gtk_main();

    /* Cleanup temp icons */
    GDir *dir = g_dir_open(gIconDir, 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            char *p = g_build_filename(gIconDir, name, NULL);
            g_unlink(p); g_free(p);
        }
        g_dir_close(dir);
    }
    g_rmdir(gIconDir);
    g_free(gIconDir);

    return 0;
}

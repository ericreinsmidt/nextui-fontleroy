/*
 * Fontleroy — Font Manager for NextUI
 *
 * Browse, preview, and install system fonts on TrimUI Brick and Smart Pro.
 * Scans bundled fonts (pak res/fonts/) and user fonts ($FONTLEROY_FONTS_DIR).
 * Backs up original system font before first install.
 *
 * Layout:
 *   Full-screen font preview (sample text rendered in the selected font)
 *   Semi-transparent bottom bar: < font name >  and button hints
 *
 * Controls:
 *   Left/Right: cycle through fonts
 *   A: install selected font as system font
 *   Y: restore original system font from backup
 *   B: exit
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"

#define PAKKIT_UI_IMPLEMENTATION
#include "pakkit_ui.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

#define MAX_FONTS        128
#define MAX_PATH_LEN    1280
#define MAX_PATH_BUF    (MAX_PATH_LEN + 64)
#define MAX_NAME_LEN     256

#define SYSTEM_FONT1     "/mnt/SDCARD/.system/res/font1.ttf"
#define SYSTEM_FONT2     "/mnt/SDCARD/.system/res/font2.ttf"

#define PREVIEW_ALPHA    "Aa Bb Cc Dd Ee Ff Gg Hh Ii Jj Kk Ll Mm Nn Oo Pp Qq Rr Ss Tt Uu Vv Ww Xx Yy Zz"
#define PREVIEW_NUMS     "0 1 2 3 4 5 6 7 8 9"
#define PREVIEW_SYMBOLS  "! @ # $ % ^ & * ( ) - _ = + [ ] { } | ; : ' \" , . < > ? / ~ `"

/* -----------------------------------------------------------------------
 * Font entry
 * ----------------------------------------------------------------------- */

typedef struct {
    char name[MAX_NAME_LEN];
    char path[MAX_PATH_LEN];
    int  is_active;
} font_entry;

static font_entry fonts[MAX_FONTS];
static int font_count = 0;

/* -----------------------------------------------------------------------
 * Paths
 * ----------------------------------------------------------------------- */

static char pak_dir[MAX_PATH_LEN];
static char user_fonts_dir[MAX_PATH_LEN];
static char pak_fonts_dir[MAX_PATH_BUF];
static char backup_path1[MAX_PATH_LEN] = SYSTEM_FONT1 ".bak";
static char backup_path2[MAX_PATH_LEN] = SYSTEM_FONT2 ".bak";

/* -----------------------------------------------------------------------
 * Preview state
 * ----------------------------------------------------------------------- */

#define PREVIEW_SIZES 5

static TTF_Font *preview_fonts[PREVIEW_SIZES];
static int       preview_loaded_index = -1;

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static int file_exists(const char *path) {
    return access(path, R_OK) == 0;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;

    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

static void strip_extension(char *name) {
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
}

static void prettify_name(char *dst, size_t dst_size, const char *filename) {
    snprintf(dst, dst_size, "%s", filename);
    strip_extension(dst);

    for (char *p = dst; *p; p++) {
        if (*p == '_' || *p == '-') *p = ' ';
    }
}

/* -----------------------------------------------------------------------
 * Font scanning
 * ----------------------------------------------------------------------- */

static void scan_directory(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && font_count < MAX_FONTS) {
        const char *name = entry->d_name;
        size_t len = strlen(name);

        if (len < 5) continue;
        const char *ext = name + len - 4;
        if (strcasecmp(ext, ".ttf") != 0 && strcasecmp(ext, ".otf") != 0) continue;

        font_entry *f = &fonts[font_count];
        snprintf(f->path, MAX_PATH_LEN, "%s/%s", dir_path, name);
        prettify_name(f->name, MAX_NAME_LEN, name);
        f->is_active = 0;
        font_count++;
    }

    closedir(d);
}

static int cmp_fonts(const void *a, const void *b) {
    return strcasecmp(((const font_entry *)a)->name, ((const font_entry *)b)->name);
}

static void detect_active_font(void) {
    if (!file_exists(SYSTEM_FONT1)) return;

    struct stat sys_stat;
    if (stat(SYSTEM_FONT1, &sys_stat) != 0) return;

    for (int i = 0; i < font_count; i++) {
        struct stat font_stat;
        if (stat(fonts[i].path, &font_stat) != 0) continue;

        if (sys_stat.st_size == font_stat.st_size) {
            FILE *sf = fopen(SYSTEM_FONT1, "rb");
            FILE *ff = fopen(fonts[i].path, "rb");
            if (!sf || !ff) {
                if (sf) fclose(sf);
                if (ff) fclose(ff);
                continue;
            }

            int match = 1;
            char buf1[4096], buf2[4096];
            size_t n1, n2;
            while ((n1 = fread(buf1, 1, sizeof(buf1), sf)) > 0) {
                n2 = fread(buf2, 1, sizeof(buf2), ff);
                if (n1 != n2 || memcmp(buf1, buf2, n1) != 0) {
                    match = 0;
                    break;
                }
            }

            fclose(sf);
            fclose(ff);

            if (match)
                fonts[i].is_active = 1;
        }
    }
}

static void load_fonts(void) {
    font_count = 0;
    scan_directory(pak_fonts_dir);
    scan_directory(user_fonts_dir);
    if (font_count > 1)
        qsort(fonts, font_count, sizeof(font_entry), cmp_fonts);
    detect_active_font();
}

/* -----------------------------------------------------------------------
 * Preview font management
 * ----------------------------------------------------------------------- */

static void free_preview_fonts(void) {
    for (int i = 0; i < PREVIEW_SIZES; i++) {
        if (preview_fonts[i]) { TTF_CloseFont(preview_fonts[i]); preview_fonts[i] = NULL; }
    }
    preview_loaded_index = -1;
}

static void load_preview_font(int index) {
    if (index == preview_loaded_index) return;
    free_preview_fonts();

    if (index < 0 || index >= font_count) return;

    /* Match NextUI's FIXED_SCALE font sizes (Brick=3, Smart Pro=2) */
    int ds = 3;
#ifdef PLATFORM_TG5040
    /* TODO: detect Brick vs Smart Pro at runtime */
#endif
    int sizes[] = { 16 * ds, 14 * ds, 12 * ds, 10 * ds, 7 * ds };

    for (int i = 0; i < PREVIEW_SIZES; i++)
        preview_fonts[i] = TTF_OpenFont(fonts[index].path, sizes[i]);

    preview_loaded_index = index;
}

/* -----------------------------------------------------------------------
 * Install / Backup / Restore
 * ----------------------------------------------------------------------- */

static int backup_exists(void) {
    return file_exists(backup_path1) && file_exists(backup_path2);
}

static int create_backup(void) {
    if (backup_exists()) return 0;

    int ok = 0;
    if (file_exists(SYSTEM_FONT1) && !file_exists(backup_path1))
        ok |= copy_file(SYSTEM_FONT1, backup_path1);
    if (file_exists(SYSTEM_FONT2) && !file_exists(backup_path2))
        ok |= copy_file(SYSTEM_FONT2, backup_path2);

    return ok;
}

static int install_font(int index) {
    if (index < 0 || index >= font_count) return -1;

    if (create_backup() != 0 && !backup_exists()) return -1;

    if (copy_file(fonts[index].path, SYSTEM_FONT1) != 0) return -1;
    copy_file(fonts[index].path, SYSTEM_FONT2);

    return 0;
}

static int restore_backup(void) {
    if (!backup_exists()) return -1;

    if (copy_file(backup_path1, SYSTEM_FONT1) != 0) return -1;
    copy_file(backup_path2, SYSTEM_FONT2);

    return 0;
}

/* -----------------------------------------------------------------------
 * Drawing — full-screen preview with bottom bar
 * ----------------------------------------------------------------------- */

static pakkit_scroll_state scroll = {0};
static int last_preview_index = -1;

static int measure_content_height(int text_w, int line_pad) {
    TTF_Font *sys_tiny = ap_get_font(AP_FONT_TINY);
    int inner_pad = ap_scale(32);
    int h = inner_pad;

    for (int t = 0; t < PREVIEW_SIZES; t++) {
        if (!preview_fonts[t]) continue;
        const char *samples[] = { PREVIEW_ALPHA, PREVIEW_NUMS, PREVIEW_SYMBOLS };

        h += TTF_FontHeight(sys_tiny) + ap_scale(2);
        for (int s = 0; s < 3; s++)
            h += ap_measure_wrapped_text_height(preview_fonts[t], samples[s], text_w) + ap_scale(2);
        h += line_pad;
    }

    return h;
}

static void draw_screen(int cursor) {
    load_preview_font(cursor);

    if (cursor != last_preview_index) {
        scroll.scroll_y = 0;
        scroll.target_scroll_y = 0;
        last_preview_index = cursor;
    }

    ap_theme *theme = ap_get_theme();
    TTF_Font *sys_small = ap_get_font(AP_FONT_SMALL);
    TTF_Font *sys_tiny  = ap_get_font(AP_FONT_TINY);
    SDL_Renderer *renderer = ap_get_renderer();
    int sw = ap_get_screen_width();
    int sh = ap_get_screen_height();
    int pad = ap_scale(16);

    /* Measure bottom bar */
    int name_line_h = TTF_FontHeight(sys_small);
    int hint_line_h = TTF_FontHeight(sys_tiny);
    int bar_h = name_line_h + hint_line_h + pad * 4;
    int bar_y = sh - bar_h;

    /* Panel covers full width, from top to just above the bar */
    int panel_y = 0;
    int panel_h = bar_y;

    ap_color panel_bg = { 245, 243, 240, 255 };
    ap_color text_dark = { 30, 30, 35, 255 };
    ap_color text_dim  = { 120, 115, 110, 255 };
    ap_draw_rect(0, panel_y, sw, panel_h, panel_bg);

    int text_x = pad * 2;
    int text_w = sw - pad * 4;
    int line_pad = ap_scale(10);

    if (preview_fonts[0]) {
        int content_h = measure_content_height(text_w, line_pad);

        pakkit_scroll_animate(&scroll);
        pakkit_scroll_update(&scroll, content_h, panel_h);

        /* Clip to panel area */
        SDL_Rect clip = { 0, panel_y, sw, panel_h };
        SDL_RenderSetClipRect(renderer, &clip);

        int y = panel_y + ap_scale(32) - scroll.scroll_y;

        const char *tier_labels[] = { "Large", "Medium", "Small", "Tiny", "Micro" };
        const char *samples[] = { PREVIEW_ALPHA, PREVIEW_NUMS, PREVIEW_SYMBOLS };

        for (int t = 0; t < PREVIEW_SIZES; t++) {
            if (!preview_fonts[t]) continue;

            TTF_SetFontStyle(preview_fonts[t], TTF_STYLE_BOLD);

            ap_draw_text(sys_tiny, tier_labels[t], text_x, y, text_dim);
            y += TTF_FontHeight(sys_tiny) + ap_scale(2);

            for (int s = 0; s < 3; s++) {
                ap_draw_text_wrapped(preview_fonts[t], samples[s],
                                     text_x, y, text_w, text_dark, AP_ALIGN_LEFT);
                y += ap_measure_wrapped_text_height(preview_fonts[t], samples[s], text_w) + ap_scale(2);
            }

            y += line_pad;
        }

        SDL_RenderSetClipRect(renderer, NULL);

        /* Scrollbar */
        if (content_h > panel_h) {
            ap_draw_scrollbar(sw - pad, panel_y + pad, panel_h - pad * 2,
                              panel_h, content_h, scroll.scroll_y);
        }

        /* Active badge */
        if (fonts[cursor].is_active) {
            ap_color badge_bg = { 60, 160, 80, 255 };
            ap_color badge_text = { 255, 255, 255, 255 };
            const char *badge = "ACTIVE";
            int badge_w = ap_measure_text(sys_tiny, badge) + pad * 2;
            int badge_h = TTF_FontHeight(sys_tiny) + ap_scale(6);
            int badge_x = sw - pad * 2 - badge_w;
            int badge_y = pad;
            ap_draw_pill(badge_x, badge_y, badge_w, badge_h, badge_bg);
            ap_draw_text(sys_tiny, badge,
                         badge_x + pad, badge_y + ap_scale(3), badge_text);
        }
    }

    /* --- Bottom bar --- */
    ap_color bar_bg = { 0, 0, 0, 255 };
    ap_draw_rect(0, bar_y, sw, bar_h, bar_bg);

    /* Font name line: < Name  (N/M) > */
    {
        int ny = bar_y + pad;
        ap_color hl = theme->highlight;
        ap_color text_color = theme->text;

        int arrow_w = ap_measure_text(sys_small, "<");

        /* Right side: counter and arrow (measure first to know available space) */
        char counter[32];
        snprintf(counter, sizeof(counter), "%d/%d", cursor + 1, font_count);
        int counter_w = ap_measure_text(sys_small, counter);
        int right_x = sw - pad * 2 - arrow_w;
        int counter_x = right_x - pad - counter_w;

        ap_draw_text(sys_small, ">", right_x, ny, hl);
        ap_draw_text(sys_small, counter, counter_x, ny, theme->hint);

        /* Left side: arrow and font name (ellipsized to fit) */
        int name_x = pad * 2 + arrow_w + pad;
        int name_max_w = counter_x - name_x - pad;

        ap_draw_text(sys_small, "<", pad * 2, ny, hl);
        ap_draw_text_ellipsized(sys_small, fonts[cursor].name,
                                name_x, ny, text_color, name_max_w);
    }

    /* Hint line */
    {
        pakkit_hint hints[] = {
            { .button = "B", .label = "Quit" },
            { .button = "Y", .label = "Restore" },
            { .button = "A", .label = "Install" },
        };
        pakkit_draw_hints(hints, 3);
    }
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    const char *env_pak   = getenv("FONTLEROY_PAK_DIR");
    const char *env_fonts = getenv("FONTLEROY_FONTS_DIR");

    snprintf(pak_dir,        MAX_PATH_LEN, "%s", env_pak   ? env_pak   : ".");
    snprintf(user_fonts_dir, MAX_PATH_LEN, "%s", env_fonts ? env_fonts : "./fonts");
    snprintf(pak_fonts_dir,  sizeof(pak_fonts_dir), "%s/res/fonts", pak_dir);

    ap_config cfg = {
        .window_title = "Fontleroy",
        .bg_image_path = NULL,
        .is_nextui = true,
    };
    if (ap_init(&cfg) != AP_OK) return 1;

    /* Splash screen */
    {
        char splash_path[MAX_PATH_BUF];
        snprintf(splash_path, sizeof(splash_path), "%s/res/splash.png", pak_dir);

        SDL_Texture *splash = ap_load_image(splash_path);
        if (splash) {
            SDL_Renderer *rend = ap_get_renderer();
            int sw = ap_get_screen_width();
            int sh = ap_get_screen_height();
            int img_w, img_h;
            SDL_QueryTexture(splash, NULL, NULL, &img_w, &img_h);

            float scale_w = (float)sw / (float)img_w;
            float scale_h = (float)sh / (float)img_h;
            float scale = (scale_w < scale_h) ? scale_w : scale_h;
            int draw_w = (int)(img_w * scale);
            int draw_h = (int)(img_h * scale);
            int x = (sw - draw_w) / 2;
            int y = (sh - draw_h) / 2;

            ap_clear_screen();
            SDL_SetRenderDrawColor(rend, 0x2f, 0x00, 0x50, 0xFF);
            SDL_Rect full = {0, 0, sw, sh};
            SDL_RenderFillRect(rend, &full);
            ap_draw_image(splash, x, y, draw_w, draw_h);
            ap_present();

            int waited = 0;
            while (waited < 900) {
                ap_input_event ev;
                while (ap_poll_input(&ev)) {
                    if (ev.pressed && !ev.repeated) waited = 900;
                }
                SDL_Delay(16);
                waited += 16;
            }
            SDL_DestroyTexture(splash);
        }
    }

    pakkit_loading("Scanning fonts...");

    load_fonts();

    if (font_count == 0) {
        pakkit_message("No fonts found.\n\nPlace .ttf files in:\nSD:/userdata/fontleroy/fonts/", "OK");
        ap_quit();
        return 0;
    }

    int cursor = 0;

    for (int i = 0; i < font_count; i++) {
        if (fonts[i].is_active) { cursor = i; break; }
    }

    int running = 1;
    while (running) {
        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (!ev.pressed) continue;

            switch (ev.button) {
                case AP_BTN_LEFT:
                    cursor = (cursor > 0) ? cursor - 1 : font_count - 1;
                    ap_request_frame();
                    break;

                case AP_BTN_RIGHT:
                    cursor = (cursor < font_count - 1) ? cursor + 1 : 0;
                    ap_request_frame();
                    break;

                case AP_BTN_UP:
                    pakkit_scroll_handle_input(&scroll, -1, PAKKIT_SCROLL_STEP);
                    ap_request_frame();
                    break;

                case AP_BTN_DOWN:
                    pakkit_scroll_handle_input(&scroll, 1, PAKKIT_SCROLL_STEP);
                    ap_request_frame();
                    break;

                case AP_BTN_A: {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                             "Install \"%s\" and quit?",
                             fonts[cursor].name);
                    if (pakkit_confirm(msg, "INSTALL", "CANCEL") == AP_OK) {
                        install_font(cursor);
                        running = 0;
                    }
                    ap_request_frame();
                    break;
                }

                case AP_BTN_Y:
                    if (backup_exists()) {
                        if (pakkit_confirm("Restore original fonts and quit?", "RESTORE", "CANCEL") == AP_OK) {
                            restore_backup();
                            running = 0;
                        }
                    } else {
                        pakkit_message("No backup found.\n\nA backup is created\nwhen you first install a font.", "OK");
                    }
                    ap_request_frame();
                    break;

                case AP_BTN_B:
                    running = 0;
                    break;

                default:
                    break;
            }
        }

        if (!running) break;

        ap_draw_background();
        draw_screen(cursor);
        ap_present();
    }

    free_preview_fonts();
    ap_quit();
    return 0;
}

/* Aura compositor — retained-mode renderer.
 *
 * Every window owns a pixel surface; apps draw into it, and the
 * compositor blits surfaces + window chrome onto the framebuffer.
 * The terminal window's surface is the console's (fb_console_surface),
 * so shell output lands in its window with no copy path changes.
 *
 * Poll-driven for now: desktop_poll_mouse() runs inside the userspace
 * input-wait loop and repaints when anything changed.
 */
#include <kernel.h>

/* palette */
#define C_BAR       0xFF0B1119
#define C_BAR_TXT   0xFF9AA7B4
#define C_TITLE     0xFF1B2634
#define C_TITLE_DIM 0xFF141C26
#define C_TITLE_TX  0xFFE8ECF1
#define C_WIN_BG    0xFF0E1420
#define C_ACCENT    0xFF2BB3A3
#define C_CURSOR    0xFFE8ECF1
#define C_CURS_OUT  0xFF0E1420
#define C_DOCK      0xFF16202C
#define C_DOT_R     0xFFE0655E
#define C_DOT_Y     0xFFE0B34E
#define C_DOT_B     0xFF5E8CE0

#define TITLE_H 28
#define BAR_H   30
#define DOCK_H  56
#define MAXW    4
#define NAPPS   4

struct win {
    bool  used;
    int   app;
    char  title[24];
    u32   accent;
    int   x, y, w, h;          /* frame rect (title + body + margins) */
    u32  *surf; int spitch;    /* body surface, (w-16) x (h-TITLE_H-16) */
    bool  is_console;          /* surf owned by fb console */
    void (*render)(struct win *);
    void (*click)(struct win *, int sx, int sy, u8 btn);
    void (*move)(struct win *, int sx, int sy, u8 btn);
};

static struct win wins[MAXW];
static int zorder[MAXW], nz;
static int focused = -1;
static int cur_x, cur_y;
static bool running;
static bool dragging;
static int drag_dx, drag_dy;
static u8 last_btn;
static u8 paint_idx;

/* ---------- cursor ---------- */

#define CUR_W 11
#define CUR_H 18
static const char *cursor_img[CUR_H] = {
    "#..........",
    "##.........",
    "#X#........",
    "#XX#.......",
    "#XXX#......",
    "#XXXX#.....",
    "#XXXXX#....",
    "#XXXXXX#...",
    "#XXXXXXX#..",
    "#XXXXXXXX#.",
    "#XXXXXXXXX#",
    "#XXXXX#####",
    "#XXX#XX#...",
    "#XX#.XX#...",
    "#X#...XX#..",
    "#X#....X#..",
    "##.....##..",
    "............",
};

static void draw_cursor(void) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            char c = cursor_img[j][i];
            if (c == 'X') px(cur_x + i, cur_y + j, C_CURSOR);
            else if (c == '#') px(cur_x + i, cur_y + j, C_CURS_OUT);
        }
}

/* ---------- painting helpers ---------- */

static void wallpaper(void) {
    int h = (int)fb_height(), w = (int)fb_width();
    for (int y = 0; y < h; y++) {
        u32 r = 0x0B + (u32)(y * 6 / (h > 0 ? h : 1));
        u32 g = 0x11 + (u32)(y * 10 / (h > 0 ? h : 1));
        u32 b = 0x19 + (u32)(y * 16 / (h > 0 ? h : 1));
        u32 c = 0xFF000000 | (r << 16) | (g << 8) | b;
        for (int x = 0; x < w; x++) px(x, y, c);
    }
}

static void draw_frame(struct win *w, bool foc) {
    fb_fill_rect(w->x, w->y, w->w, TITLE_H, foc ? C_TITLE : C_TITLE_DIM);
    fb_fill_rect(w->x, w->y, w->w, 1, w->accent);
    fb_text(w->x + 12, w->y + 6, w->title, foc ? C_TITLE_TX : C_BAR_TXT, 1);
    fb_fill_rect(w->x + w->w - 60, w->y + 10, 8, 8, C_DOT_R);
    fb_fill_rect(w->x + w->w - 40, w->y + 10, 8, 8, C_DOT_Y);
    fb_fill_rect(w->x + w->w - 20, w->y + 10, 8, 8, w->accent);
    fb_fill_rect(w->x, w->y + TITLE_H, w->w, w->h - TITLE_H, C_WIN_BG);
}

static void blit(u32 *src, int spitch, int dx, int dy, int w, int h) {
    if (!src) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px(dx + x, dy + y, src[y * spitch + x]);
}

static int dock_w(void) { return NAPPS * 44 + (NAPPS + 1) * 12; }
static int dock_x(void) { return ((int)fb_width() - dock_w()) / 2; }
static int dock_y(void) { return (int)fb_height() - DOCK_H - 8; }

static void draw_dock(void) {
    u32 cols[NAPPS] = { C_ACCENT, C_DOT_B, C_DOT_R, C_DOT_Y };
    const char *ch[NAPPS] = { "T", "F", "P", "A" };
    int dx0 = dock_x(), dy0 = dock_y();
    fb_fill_rect(dx0 - 12, dy0 - 8, dock_w() + 24, DOCK_H + 16, C_DOCK);
    fb_fill_rect(dx0 - 12, dy0 - 8, dock_w() + 24, 1, 0xFF223040);
    for (int i = 0; i < NAPPS; i++) {
        int ix = dx0 + 12 + i * (44 + 12);
        fb_fill_rect(ix, dy0, 44, 44, 0xFF0E1620);
        fb_fill_rect(ix, dy0, 44, 2, cols[i]);
        fb_text(ix + 18, dy0 + 14, ch[i], cols[i], 1);
        /* running indicator */
        for (int k = 0; k < MAXW; k++)
            if (wins[k].used && wins[k].app == i)
                fb_fill_rect(ix + 20, dy0 + 48, 4, 4, cols[i]);
    }
}

static void draw_chrome(void) {
    fb_fill_rect(0, 0, (int)fb_width(), BAR_H, C_BAR);
    fb_text(12, 7, "AequiOS", 0xFFE8ECF1, 1);
    if (focused >= 0)
        fb_text(96, 7, wins[focused].title, C_ACCENT, 1);
    fb_text((int)fb_width() - 200, 7, "Ordo kernel  -  Serenus", C_BAR_TXT, 1);
    fb_fill_rect(0, BAR_H, (int)fb_width(), 1, C_ACCENT);
    draw_dock();
}

static void repaint_all(void) {
    wallpaper();
    draw_chrome();
    for (int zi = 0; zi < nz; zi++) {
        struct win *w = &wins[zorder[zi]];
        if (!w->used) continue;
        draw_frame(w, zorder[zi] == focused);
        u32 *s = w->surf; int sp = w->spitch;
        int sw = w->w - 16, sh = w->h - TITLE_H - 16;
        if (w->is_console) {
            int pw, ph;
            s = fb_console_surface(&sp, &pw, &ph);
            sw = pw; sh = ph;
        }
        blit(s, sp, w->x + 8, w->y + TITLE_H + 8, sw, sh);
    }
    draw_cursor();
    fb_console_clean();
}

/* ---------- apps ---------- */

static void surf_puts(struct win *w, int x, int y, const char *s, u32 c) {
    fb_dst_set(w->surf, (u64)w->spitch, (u64)w->w, (u64)w->h);
    fb_text(x, y, s, c, 1);
    fb_dst_set(NULL, 0, 0, 0);
}

static void surf_fill(struct win *w, int x, int y, int fw, int fh, u32 c) {
    fb_dst_set(w->surf, (u64)w->spitch, (u64)w->w, (u64)w->h);
    fb_fill_rect(x, y, fw, fh, c);
    fb_dst_set(NULL, 0, 0, 0);
}

/* -- Files: initramfs listing -- */
static struct win *files_w;
static int files_row;
static void files_cb(const char *name, size_t size) {
    char line[96];
    snprintf(line, sizeof(line), "  %-36s %6lu B", name, (unsigned long)size);
    surf_puts(files_w, 8, 8 + files_row * 18, line, 0xFFE8ECF1);
    files_row++;
}
static void files_render(struct win *w) {
    files_w = w; files_row = 0;
    surf_fill(w, 0, 0, w->w, w->h, C_WIN_BG);
    surf_puts(w, 8, 8, "initramfs:", C_ACCENT);
    files_row = 1;
    vfs_list(files_cb);
}

/* -- Paint: click/drag dabs colour -- */
static const u32 paint_pal[6] = {
    C_ACCENT, C_DOT_B, C_DOT_R, C_DOT_Y, 0xFFB48CE0, 0xFFE8ECF1,
};
static void paint_render(struct win *w) {
    surf_fill(w, 0, 0, w->w, w->h, 0xFF101820);
    surf_puts(w, 8, 8, "drag to paint - click cycles colour", C_BAR_TXT);
}
static void paint_dab(struct win *w, int sx, int sy, u8 btn) {
    if (!(btn & 1) || sy < 28) return;
    u32 c = paint_pal[paint_idx % 6];
    surf_fill(w, sx - 4, sy - 4, 8, 8, c);
}
static void paint_click(struct win *w, int sx, int sy, u8 btn) {
    paint_idx++;
    paint_dab(w, sx, sy, btn);
}

/* -- About card -- */
static void about_render(struct win *w) {
    surf_fill(w, 0, 0, w->w, w->h, C_WIN_BG);
    surf_puts(w, 8, 12, "AequiOS", 0xFFE8ECF1);
    surf_puts(w, 8, 34, "v0.1 - Ordo kernel / Serenus userspace", C_BAR_TXT);
    surf_puts(w, 8, 56, "Even in Architecture, Design, and Spirit", C_ACCENT);
    surf_puts(w, 8, 92, "Scratch kernel, x86_64 + aarch64.", C_BAR_TXT);
    surf_puts(w, 8, 112, "Linux syscall-compat personality.", C_BAR_TXT);
}

struct app_desc {
    const char *title; u32 accent; int w, h;
    void (*render)(struct win *);
    void (*click)(struct win *, int, int, u8);
    void (*move)(struct win *, int, int, u8);
};
static const struct app_desc apps[NAPPS] = {
    { "Ratio - shell", C_ACCENT, 0, 0, NULL, NULL, NULL },          /* console */
    { "Files",         C_DOT_B,  480, 300, files_render, NULL, NULL },
    { "Paint",         C_DOT_R,  520, 380, paint_render, paint_click, paint_dab },
    { "About",         C_DOT_Y,  380, 220, about_render, NULL, NULL },
};

static void raise_focus(int wi) {
    /* move wi to top of zorder */
    int pos = -1;
    for (int i = 0; i < nz; i++) if (zorder[i] == wi) pos = i;
    if (pos >= 0) {
        for (int i = pos; i < nz - 1; i++) zorder[i] = zorder[i + 1];
        nz--;
    }
    zorder[nz++] = wi;
    focused = wi;
}

static void open_win(int app, int wi) {
    struct win *w = &wins[wi];
    const struct app_desc *d = &apps[app];
    memset(w, 0, sizeof(*w));
    w->used = true; w->app = app;
    strncpy(w->title, d->title, sizeof(w->title) - 1);
    w->accent = d->accent;
    if (app == 0) {
        /* console: 3/4 x 2/3 centred, surface is fb console's */
        w->w = ((int)fb_width() * 3) / 4;
        w->h = ((int)fb_height() * 2) / 3;
        w->x = ((int)fb_width() - w->w) / 2;
        w->y = BAR_H + 40;
        w->is_console = true;
        int sp, sw, sh;
        u32 *s = fb_console_surface(&sp, &sw, &sh);
        (void)s;
        fb_set_console_rect(0, 0, w->w - 16, w->h - TITLE_H - 16);
        fb_repaint();
    } else {
        w->w = d->w; w->h = d->h;
        w->x = 80 + app * 56; w->y = BAR_H + 44 + app * 40;
        w->spitch = w->w - 16;
        w->surf = kmalloc((size_t)(w->w - 16) * (w->h - TITLE_H - 16) * 4);
        if (w->surf)
            memset(w->surf, 0, (size_t)(w->w - 16) * (w->h - TITLE_H - 16) * 4);
        if (w->render) w->render(w);
    }
    w->click = d->click;
    w->move = d->move;
    raise_focus(wi);
}

static void launch(int app) {
    for (int i = 0; i < MAXW; i++)
        if (wins[i].used && wins[i].app == app) { raise_focus(i); repaint_all(); return; }
    for (int i = 0; i < MAXW; i++)
        if (!wins[i].used) { open_win(app, i); repaint_all(); return; }
}

static int dock_hit(int x, int y) {
    int dy0 = dock_y(), dx0 = dock_x();
    if (y < dy0 - 8 || y > dy0 + DOCK_H + 8) return -1;
    for (int i = 0; i < NAPPS; i++) {
        int ix = dx0 + 12 + i * (44 + 12);
        if (x >= ix && x < ix + 44) return i;
    }
    return -1;
}

static void handle_click(u8 btn) {
    int di = dock_hit(cur_x, cur_y);
    if (di >= 0) { launch(di); return; }
    /* topmost window containing the point */
    for (int zi = nz - 1; zi >= 0; zi--) {
        struct win *w = &wins[zorder[zi]];
        if (!w->used) continue;
        if (cur_x < w->x || cur_x >= w->x + w->w ||
            cur_y < w->y || cur_y >= w->y + w->h) continue;
        raise_focus(zorder[zi]);
        if (cur_y < w->y + TITLE_H) {
            dragging = true;
            drag_dx = cur_x - w->x; drag_dy = cur_y - w->y;
        } else if (w->click) {
            w->click(w, cur_x - w->x - 8, cur_y - w->y - TITLE_H - 8, btn);
        }
        repaint_all();
        return;
    }
    repaint_all();
}

/* Called from the input wait loop: poll the mouse, repaint when dirty. */
void desktop_poll_mouse(void) {
    if (!running || !fb_ready()) return;
    s32 dx = 0, dy = 0; u8 btn = last_btn;
    bool moved = false;
    while (input_poll_mouse(&dx, &dy, &btn)) {
        cur_x += dx; cur_y += dy;
        moved = true;
    }
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    if (cur_x > (int)fb_width() - 2)  cur_x = (int)fb_width() - 2;
    if (cur_y > (int)fb_height() - 2) cur_y = (int)fb_height() - 2;

    bool pressed  = (btn & 1) && !(last_btn & 1);
    bool released = !(btn & 1) && (last_btn & 1);
    if (released) dragging = false;

    if (pressed) { last_btn = btn; handle_click(btn); return; }
    last_btn = btn;

    if (dragging && (btn & 1) && moved) {
        int wi = focused;
        if (wi >= 0) {
            struct win *w = &wins[wi];
            w->x = cur_x - drag_dx; w->y = cur_y - drag_dy;
            if (w->x < 0) w->x = 0;
            if (w->y < BAR_H + 1) w->y = BAR_H + 1;
            if (w->x > (int)fb_width() - 40) w->x = (int)fb_width() - 40;
            if (w->y > (int)fb_height() - 40) w->y = (int)fb_height() - 40;
        }
        repaint_all();
        return;
    }

    /* continuous paint strokes: button held over a movable-content app */
    if ((btn & 1) && focused >= 0 && wins[focused].move && moved) {
        struct win *w = &wins[focused];
        int sx = cur_x - w->x - 8, sy = cur_y - w->y - TITLE_H - 8;
        if (sx >= 0 && sx < w->w - 16 && sy >= 0 && sy < w->h - TITLE_H - 16)
            w->move(w, sx, sy, btn);
        repaint_all();
        return;
    }

    if (moved || fb_console_dirty()) repaint_all();
}

void desktop_init(void) {
    if (!fb_ready()) return;
    cur_x = (int)fb_width() / 2;
    cur_y = (int)fb_height() / 2;
    open_win(0, 0);            /* terminal */
    running = true;
    repaint_all();
}

bool desktop_running(void) { return running; }

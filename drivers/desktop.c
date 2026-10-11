/* Aura v1 — desktop compositor: wallpaper, menu bar, dock, one
 * draggable terminal window, hardware cursor.
 *
 * Deliberately tiny: a single full-screen repaint path plus a cheap
 * cursor overlay. Windows are rects; the Ratio shell's console renders
 * inside the terminal window's body via fb_set_console_rect(). */
#include <kernel.h>

/* palette — calm navy/teal, matching the splash */
#define C_BG_TOP    0xFF101827
#define C_BG_BOT    0xFF0A1C22
#define C_BAR       0xFF0C1220
#define C_BAR_TXT   0xFF8C96A3
#define C_WIN_BG    0xFF0E1420
#define C_TITLE     0xFF1A2432
#define C_TITLE_TX  0xFFE8ECF1
#define C_ACCENT    0xFF2BB3A3
#define C_CURSOR    0xFFE8ECF1
#define C_CURS_OUT  0xFF0E1420
#define C_DOCK      0xFF16202C

#define TITLE_H 28
#define BAR_H   30
#define DOCK_H  64


static int win_x, win_y, win_w, win_h;    /* whole window incl. title */
static int cur_x, cur_y;
static bool have_mouse;
static bool dragging;
static int drag_dx, drag_dy;
static u8 last_btn;

/* arrow cursor, 11x18 — '#' drawn, '.' transparent edge */
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
    "#XXXXX#####",
    "#XX#XX#....",
    "#X#.#XX#...",
    "##..#XX#...",
    "#....#X#...",
    ".....#X#...",
    "......##...",
    "...........",
};
static u32 cur_under[CUR_W * CUR_H];
static bool cur_drawn;

static void wallpaper(void) {
    u64 h = fb_height(), w = fb_width();
    for (u64 y = 0; y < h; y++) {
        u32 t = (u32)(y * 255 / (h ? h : 1));
        u8 r = (u8)(((C_BG_TOP >> 16) & 0xFF) + (((C_BG_BOT >> 16) & 0xFF) - ((C_BG_TOP >> 16) & 0xFF)) * (s32)t / 255);
        u8 g = (u8)(((C_BG_TOP >> 8) & 0xFF) + (((C_BG_BOT >> 8) & 0xFF) - ((C_BG_TOP >> 8) & 0xFF)) * (s32)t / 255);
        u8 b = (u8)((C_BG_TOP & 0xFF) + ((C_BG_BOT & 0xFF) - (C_BG_TOP & 0xFF)) * (s32)t / 255);
        fb_fill_rect(0, (int)y, (int)w, 1, 0xFF000000 | ((u32)r << 16) | ((u32)g << 8) | b);
    }
}

static void draw_cursor_at(int x, int y) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            int px_ = x + i, py_ = y + j;
            u64 xx = (u64)px_, yy = (u64)py_;
            if (px_ < 0 || py_ < 0 || xx >= fb_width() || yy >= fb_height()) {
                cur_under[j * CUR_W + i] = 0;
                continue;
            }
            cur_under[j * CUR_W + i] = fb_get(px_, py_);
            char c = cursor_img[j][i];
            if (c == 'X') px(px_, py_, C_CURSOR);
            else if (c == '#') px(px_, py_, C_CURS_OUT);
        }
    cur_drawn = true;
    cur_x = x; cur_y = y;
}

static void undraw_cursor(void) {
    if (!cur_drawn) return;
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            int x = cur_x + i, y = cur_y + j;
            if (x < 0 || y < 0 || (u64)x >= fb_width() || (u64)y >= fb_height())
                continue;
            px(x, y, cur_under[j * CUR_W + i]);
        }
    cur_drawn = false;
}

/* window frame + body; console grid repaints itself into the body */
static void draw_window(void) {
    fb_fill_rect(win_x, win_y, win_w, TITLE_H, C_TITLE);
    fb_fill_rect(win_x, win_y, win_w, 1, C_ACCENT);
    fb_text(win_x + 12, win_y + 6, "Ratio - shell", C_TITLE_TX, 1);
    /* window control dots */
    fb_fill_rect(win_x + win_w - 60, win_y + 10, 8, 8, 0xFFE0655E);
    fb_fill_rect(win_x + win_w - 40, win_y + 10, 8, 8, 0xFFE0B34E);
    fb_fill_rect(win_x + win_w - 20, win_y + 10, 8, 8, C_ACCENT);

    fb_fill_rect(win_x, win_y + TITLE_H, win_w, win_h - TITLE_H, C_WIN_BG);
    fb_set_console_rect(win_x + 8, win_y + TITLE_H + 8,
                        win_w - 16, win_h - TITLE_H - 16);
    fb_repaint();
}

static void draw_chrome(void) {
    /* menu bar */
    fb_fill_rect(0, 0, (int)fb_width(), BAR_H, C_BAR);
    fb_text(12, 7, "AequiOS", 0xFFE8ECF1, 1);
    fb_text((int)fb_width() - 200, 7, "Ordo kernel  -  Serenus", C_BAR_TXT, 1);
    fb_fill_rect(0, BAR_H, (int)fb_width(), 1, C_ACCENT);

    /* dock — decorative icon strip, bottom center */
    int dock_w = 4 * 44 + 5 * 12;
    int dx0 = ((int)fb_width() - dock_w) / 2;
    int dy0 = (int)fb_height() - DOCK_H - 8;
    fb_fill_rect(dx0 - 12, dy0 - 8, dock_w + 24, DOCK_H + 16, C_DOCK);
    fb_fill_rect(dx0 - 12, dy0 - 8, dock_w + 24, 1, 0xFF223040);
    u32 icon_colors[4] = { C_ACCENT, 0xFFE0655E, 0xFFE0B34E, 0xFF5E8CE0 };
    const char *icon_names[4] = { "T", "F", "M", "S" };
    for (int i = 0; i < 4; i++) {
        int ix = dx0 + 12 + i * (44 + 12);
        fb_fill_rect(ix, dy0, 44, 44, 0xFF0E1620);
        fb_fill_rect(ix, dy0, 44, 2, icon_colors[i]);
        fb_text(ix + 18, dy0 + 16, icon_names[i], icon_colors[i], 1);
    }
}

void desktop_init(void) {
    if (!fb_ready()) return;
    wallpaper();
    draw_chrome();

    int w = (int)fb_width(), h = (int)fb_height();
    win_w = (w * 3) / 4;  win_h = (h * 2) / 3;
    win_x = (w - win_w) / 2;
    win_y = BAR_H + 40;
    draw_window();

    cur_x = w / 2; cur_y = h / 2;
    draw_cursor_at(cur_x, cur_y);
    have_mouse = true;
}

static void repaint_all(void) {
    wallpaper();
    draw_chrome();
    draw_window();
}

/* Called from the input wait loop: move the cursor, drag the window
 * by its title bar. */
void desktop_poll_mouse(void) {
    s32 dx, dy; u8 btn;
    if (!have_mouse || !fb_ready()) return;
    if (!input_poll_mouse(&dx, &dy, &btn)) return;

    undraw_cursor();
    cur_x += dx; cur_y += dy;
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    if (cur_x > (int)fb_width() - 2)  cur_x = (int)fb_width() - 2;
    if (cur_y > (int)fb_height() - 2) cur_y = (int)fb_height() - 2;

    bool pressed = (btn & 1) && !(last_btn & 1);
    bool released = !(btn & 1) && (last_btn & 1);
    if (pressed && cur_y >= win_y && cur_y < win_y + TITLE_H &&
        cur_x >= win_x && cur_x < win_x) {
        dragging = true;
        drag_dx = cur_x - win_x; drag_dy = cur_y - win_y;
    }
    if (released) dragging = false;
    last_btn = btn;

    if (dragging && (btn & 1)) {
        int nx = cur_x - drag_dx, ny = cur_y - drag_dy;
        if (nx != win_x || ny != win_y) {
            win_x = nx; win_y = ny;
            if (win_x < 0) win_x = 0;
            if (win_y < BAR_H + 1) win_y = BAR_H + 1;
            if (win_x > (int)fb_width() - 40) win_x = (int)fb_width() - 40;
            if (win_y > (int)fb_height() - 40) win_y = (int)fb_height() - 40;
            repaint_all();
        }
    }
    draw_cursor_at(cur_x, cur_y);
}

bool desktop_running(void) { return have_mouse; }

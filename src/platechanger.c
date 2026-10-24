/*
 * PlateChanger.asi - change the LibertyCityPlates plate of the car you're in
 * (or the last car you drove) with a hotkey, and keep it on your saved cars.
 * For GTA IV / EFLC, including Complete Edition 1.2.0.59 with FusionFix.
 *
 * How it works
 *   - Finds the game's script command table (same byte patterns FusionFix
 *     uses for 1.0.x and 1.2.x) and calls commands by hash.
 *   - Hooks the game window's message handler and runs a timer there, on the
 *     game's main thread between frames, where FusionFix also calls script
 *     commands from.
 *   - Editing happens in-game: the plate changes on the car as you type,
 *     Page Up/Down step through plate pictures, Home/End through fonts.
 *     Picking a picture also applies its text size, position and colour from
 *     the LibertyCityPlates vehicles.ide.
 *   - Cars you edit are remembered in PlateChanger.plates with their model,
 *     colours and the spots where they've stood parked. When the game
 *     creates a car with the same model and colours at one of those spots
 *     (that's how saved cars come back), the plate is put back on it.
 *
 * Settings: PlateChanger.ini next to this file. Log: PlateChanger.log.
 * Plain Win32, no C runtime needed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* no C runtime: the compiler may still emit calls to these */
void *memset(void *d, int c, unsigned int n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned int n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }

#define VERSION "1.4"

/* ------------------------------------------------------------ logging */
static char g_dir[MAX_PATH];
static char g_logPath[MAX_PATH];
static char g_iniPath[MAX_PATH];
static char g_platesPath[MAX_PATH];

static void plog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    lstrcatA(buf, "\r\n");
    HANDLE h = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, buf, lstrlenA(buf), &w, NULL);
    CloseHandle(h);
}

/* ------------------------------------------------------------ small helpers */
typedef union { float f; DWORD d; } FBits;
static DWORD fbits(float f) { FBits b; b.f = f; return b.d; }

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ','; }

/* read the next token from *p (space/tab/comma separated); returns length, 0 at end of line */
static int next_token(const char **p, char *out, int max)
{
    const char *s = *p;
    while (*s == ' ' || *s == '\t' || *s == ',') s++;
    int n = 0;
    while (*s && !is_space(*s)) { if (n < max - 1) out[n++] = *s; s++; }
    out[n] = 0;
    *p = s;
    return n;
}

static int parse_int(const char *s, int *v)
{
    int neg = 0, n = 0, any = 0;
    if (*s == '-') { neg = 1; s++; }
    for (; *s >= '0' && *s <= '9'; s++) { n = n * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return 0;
    *v = neg ? -n : n;
    return 1;
}

static int parse_float(const char *s, float *v)
{
    int neg = 0, any = 0;
    float r = 0, scale = 1;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    for (; *s >= '0' && *s <= '9'; s++) { r = r * 10 + (float)(*s - '0'); any = 1; }
    if (*s == '.') { s++; for (; *s >= '0' && *s <= '9'; s++) { scale /= 10; r += (float)(*s - '0') * scale; any = 1; } }
    if (!any || *s) return 0;
    *v = neg ? -r : r;
    return 1;
}

/* read a whole text file into a heap buffer (NUL-terminated) */
static char *read_file(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(h, NULL), got = 0;
    char *buf = (size != INVALID_FILE_SIZE && size < 8 * 1024 * 1024) ? (char *)HeapAlloc(GetProcessHeap(), 0, size + 1) : NULL;
    if (buf && ReadFile(h, buf, size, &got, NULL)) buf[got] = 0;
    else if (buf) { HeapFree(GetProcessHeap(), 0, buf); buf = NULL; }
    CloseHandle(h);
    return buf;
}

/* ------------------------------------------------------------ pattern scan */
static BYTE *g_base;
static DWORD g_size;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* pattern like "8B 3D ? ? ? ? 85 FF" ; returns the nth match (0 = first) or NULL,
   and the total number of matches in *count if count isn't NULL */
static BYTE *find_pattern_n(const char *pat, int nth, int *count)
{
    BYTE bytes[96]; BYTE mask[96]; int n = 0;
    for (const char *p = pat; *p && n < 96;) {
        if (*p == ' ') { p++; continue; }
        if (*p == '?') { bytes[n] = 0; mask[n++] = 0; p++; if (*p == '?') p++; continue; }
        int hi = hexval(p[0]), lo = hexval(p[1]);
        if (hi < 0 || lo < 0) return NULL;
        bytes[n] = (BYTE)(hi * 16 + lo); mask[n++] = 1; p += 2;
    }
    BYTE *hit = NULL; int found = 0;
    for (DWORD i = 0; i + n <= g_size; i++) {
        int k = 0;
        while (k < n && (!mask[k] || g_base[i + k] == bytes[k])) k++;
        if (k == n) {
            if (found == nth) hit = g_base + i;
            found++;
            if (!count && hit) break;
        }
    }
    if (count) *count = found;
    return hit;
}
static BYTE *find_pattern(const char *pat) { return find_pattern_n(pat, 0, NULL); }

/* ------------------------------------------------------------ script commands */
static DWORD  *g_pTableSize;   /* rage::scrEngine table size   */
static DWORD **g_ppTable;      /* rage::scrEngine table (hash, handler pairs) */
static BYTE   *g_pUserPause, *g_pScriptPause;

typedef struct {
    void  *ret;
    DWORD  argc;
    void  *args;
    DWORD  dataCount;
    void  *orig[4];
    float  temp[4][4];
    DWORD  stack[16];
} NCtx;
typedef void (__cdecl *NHandler)(NCtx *);

typedef void *(__stdcall *GetNativeAddressFn)(DWORD hash);
static GetNativeAddressFn g_getNativeAddress;

static NHandler get_handler(DWORD hash)
{
    if (g_getNativeAddress) return (NHandler)g_getNativeAddress(hash);
    if (!g_pTableSize || !g_ppTable) return NULL;
    DWORD size = *g_pTableSize;
    DWORD *tbl = *g_ppTable;
    if (!size || !tbl || !hash) return NULL;
    DWORD idx = hash % size, h = hash, e = tbl[2 * idx];
    if (e != hash) {
        while (e) {
            h = (h >> 1) + 1;
            idx = (h + idx) % size;
            e = tbl[2 * idx];
            if (e == hash) break;
        }
    }
    if (!e) return NULL;
    return (NHandler)tbl[2 * idx + 1];
}

/* call a command with up to 5 32-bit arguments; returns the first result */
static int g_missing;
static DWORD call(DWORD hash, int argc, DWORD a0, DWORD a1, DWORD a2, DWORD a3, DWORD a4)
{
    NHandler fn = get_handler(hash);
    if (!fn) { g_missing = 1; plog("script command %08X not found", hash); return 0; }
    NCtx c;
    ZeroMemory(&c, sizeof(c));
    c.args = c.stack; c.ret = c.stack; c.argc = (DWORD)argc;
    DWORD a[5] = { a0, a1, a2, a3, a4 };
    for (int i = 0; i < argc; i++) c.stack[i] = a[i];
    fn(&c);
    return c.stack[0];
}
#define C0(h)                 call(h, 0, 0, 0, 0, 0, 0)
#define C1(h, a)              call(h, 1, (DWORD)(a), 0, 0, 0, 0)
#define C2(h, a, b)           call(h, 2, (DWORD)(a), (DWORD)(b), 0, 0, 0)
#define C3(h, a, b, c)        call(h, 3, (DWORD)(a), (DWORD)(b), (DWORD)(c), 0, 0)
#define C4(h, a, b, c, d)     call(h, 4, (DWORD)(a), (DWORD)(b), (DWORD)(c), (DWORD)(d), 0)
#define C5(h, a, b, c, d, e)  call(h, 5, (DWORD)(a), (DWORD)(b), (DWORD)(c), (DWORD)(d), (DWORD)(e))

/* game commands (hashes as used by GTA IV) */
#define GET_PLAYER_ID                        0x62E319C6
#define CONVERT_INT_TO_PLAYERINDEX           0x5996315E
#define IS_PLAYER_PLAYING                    0x08274BA4
#define GET_PLAYER_CHAR                      0x511454A9
#define IS_CHAR_IN_ANY_CAR                   0x71184DA3
#define GET_CAR_CHAR_IS_USING                0x1B067237
#define GET_PLAYERS_LAST_CAR_NO_SAVE         0x12067E8D
#define DOES_VEHICLE_EXIST                   0x67A42263
#define GET_CAR_MODEL                        0x5FF84497
#define GET_CAR_COLOURS                      0x6CAC3D62
#define GET_EXTRA_CAR_COLOURS                0x25B87BCA
#define GET_CAR_COORDINATES                  0x2D432EAB
#define GET_CAR_SPEED                        0x16DD2D00
#define PRINT_STRING_WITH_LITERAL_STRING_NOW 0x0CA539D6
#define SET_PLAYER_CONTROL                   0x1A6203EA
#define IS_PLAYER_CONTROL_ON                 0x30CD2F1F
/* LibertyCityPlates commands (registered by LibertyCityPlates.asi) */
#define LCP_TEXT_0123                        0x09DED943
#define LCP_TEXT_4567                        0x09DE9587
#define LCP_TEXT_891011                      0x0E515A91
#define LCP_TEXT_TINT_FLOAT4                 0x3FFFB335
#define LCP_TEXT_LENGTH                      0x46F3540C
#define LCP_TEXT_TEXTURE                     0xF055490B  /* font */
#define LCP_PLATE_TEXTURE                    0xC621B628  /* plate picture */
#define LCP_TEXT_SCALE                       0x6C688F74
#define LCP_TEXT_OFFSET                      0x04C26575

static char g_msg[160];
static char g_gxtString[] = "STRING";
static void show(const char *text)
{
    lstrcpynA(g_msg, text, sizeof(g_msg));
    C4(PRINT_STRING_WITH_LITERAL_STRING_NOW, g_gxtString, g_msg, 4000, 1);
}

static int game_paused(void)
{
    return (g_pUserPause && *g_pUserPause) || (g_pScriptPause && *g_pScriptPause);
}

/* ------------------------------------------------------------ vehicle pool */
typedef struct {
    BYTE *objects;
    BYTE *flags;
    DWORD size;
    DWORD entrySize;
} Pool;
static DWORD *g_ppVehPool;   /* variable that holds the vehicle pool pointer */

static Pool *veh_pool(void)
{
    if (!g_ppVehPool) return NULL;
    Pool *p = (Pool *)*g_ppVehPool;
    if (!p || !p->flags || !p->size || p->size > 4096) return NULL;
    return p;
}

/* ------------------------------------------------------------ settings */
static UINT g_vk = VK_F11;
static int g_needShift, g_needCtrl, g_needAlt;
static int g_maxPic = -1, g_maxFont = -1;   /* -1 = automatic, from vehicles.ide */
static int g_remember = 1;
static char g_ideSetting[MAX_PATH];

static void load_settings(void)
{
    char key[64];
    GetPrivateProfileStringA("PlateChanger", "Key", "F11", key, sizeof(key), g_iniPath);
    CharUpperA(key);
    g_needShift = g_needCtrl = g_needAlt = 0;
    char *tok = key, *p = key;
    for (;;) {
        if (*p == '+' || *p == 0) {
            char save = *p; *p = 0;
            while (*tok == ' ') tok++;
            if (!lstrcmpA(tok, "SHIFT")) g_needShift = 1;
            else if (!lstrcmpA(tok, "CTRL")) g_needCtrl = 1;
            else if (!lstrcmpA(tok, "ALT")) g_needAlt = 1;
            else if (tok[0] == 'F' && tok[1] >= '1' && tok[1] <= '9') {
                int f = tok[1] - '0';
                if (tok[2] >= '0' && tok[2] <= '9') f = f * 10 + (tok[2] - '0');
                if (f >= 1 && f <= 24) g_vk = VK_F1 + f - 1;
            } else if (tok[0] && !tok[1]) {
                g_vk = (UINT)(BYTE)tok[0];   /* single letter or digit */
            }
            if (!save) break;
            tok = p + 1;
        }
        p++;
    }
    char v[16];
    GetPrivateProfileStringA("PlateChanger", "MaxPicture", "", v, sizeof(v), g_iniPath);
    if (!v[0] || !parse_int(v, &g_maxPic) || g_maxPic < 0 || g_maxPic > 253) g_maxPic = -1;
    GetPrivateProfileStringA("PlateChanger", "MaxFont", "", v, sizeof(v), g_iniPath);
    if (!v[0] || !parse_int(v, &g_maxFont) || g_maxFont < 0 || g_maxFont > 253) g_maxFont = -1;
    g_remember = (int)GetPrivateProfileIntA("PlateChanger", "RememberPlates", 1, g_iniPath);
    GetPrivateProfileStringA("PlateChanger", "PlatesIde", "", g_ideSetting, sizeof(g_ideSetting), g_iniPath);
    plog("hotkey: %s%s%svk 0x%02X, remember plates %s",
         g_needCtrl ? "Ctrl+" : "", g_needAlt ? "Alt+" : "", g_needShift ? "Shift+" : "", g_vk,
         g_remember ? "on" : "off");
}

/* ------------------------------------------------------------ plate styles from vehicles.ide */
typedef struct {
    int have;
    int font;
    float sx, sy, ox, oy, r, g, b, a;
} Style;
static Style g_styles[254];

static void load_styles(void)
{
    char exeDir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    char *slash = exeDir;
    for (char *s = exeDir; *s; s++) if (*s == '\\' || *s == '/') slash = s;
    *slash = 0;

    const char *tryPaths[3]; char p0[MAX_PATH], p1[MAX_PATH], p2[MAX_PATH];
    int n = 0;
    if (g_ideSetting[0]) {
        if (g_ideSetting[1] == ':' || g_ideSetting[0] == '\\') lstrcpynA(p0, g_ideSetting, MAX_PATH);
        else wsprintfA(p0, "%s\\%s", exeDir, g_ideSetting);
        tryPaths[n++] = p0;
    }
    wsprintfA(p1, "%s\\update\\common\\data\\vehicles.ide", exeDir); tryPaths[n++] = p1;
    wsprintfA(p2, "%s\\common\\data\\vehicles.ide", exeDir); tryPaths[n++] = p2;

    char *buf = NULL;
    for (int i = 0; i < n && !buf; i++) { buf = read_file(tryPaths[i]); if (buf) lstrcpynA(path, tryPaths[i], MAX_PATH); }
    if (!buf) { plog("plate styles: vehicles.ide not found, pictures keep the car's original text layout"); return; }

    int inSection = 0, count = 0;
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char save = *end; *end = 0;
        const char *p = line;
        char t[32];
        if (next_token(&p, t, sizeof(t))) {
            if (!lstrcmpiA(t, "license_plates")) inSection = 1;
            else if (!lstrcmpiA(t, "end")) inSection = 0;
            else if (inSection && t[0] != '#') {
                /* id parent chance platetxd font sx sy ox oy r g b a format ... */
                char tok[13][32]; int k = 0;
                lstrcpynA(tok[k++], t, 32);
                while (k < 13 && next_token(&p, tok[k], 32)) k++;
                int pic, font; Style s;
                if (k == 13 && parse_int(tok[3], &pic) && parse_int(tok[4], &font) &&
                    parse_float(tok[5], &s.sx) && parse_float(tok[6], &s.sy) &&
                    parse_float(tok[7], &s.ox) && parse_float(tok[8], &s.oy) &&
                    parse_float(tok[9], &s.r) && parse_float(tok[10], &s.g) &&
                    parse_float(tok[11], &s.b) && parse_float(tok[12], &s.a) &&
                    pic >= 0 && pic < 254 && !g_styles[pic].have) {
                    s.have = 1; s.font = font;
                    g_styles[pic] = s;
                    count++;
                }
            }
        }
        *end = save;
        line = *end ? end + 1 : end;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    plog("plate styles: %d pictures from %s", count, path);
}

/* the pictures and fonts Page Up/Down and Home/End step through */
static BYTE g_picList[254], g_fontList[254];
static int g_npic, g_nfont;

static void build_lists(void)
{
    BYTE usedFont[254] = { 0 };
    g_npic = g_nfont = 0;
    if (g_maxPic >= 0) for (int i = 0; i <= g_maxPic; i++) g_picList[g_npic++] = (BYTE)i;
    else for (int i = 0; i < 254; i++) if (g_styles[i].have) g_picList[g_npic++] = (BYTE)i;
    if (!g_npic) for (int i = 0; i <= 19; i++) g_picList[g_npic++] = (BYTE)i;   /* stock LibertyCityPlates */

    if (g_maxFont >= 0) for (int i = 0; i <= g_maxFont; i++) g_fontList[g_nfont++] = (BYTE)i;
    else {
        for (int i = 0; i < 254; i++) if (g_styles[i].have && g_styles[i].font >= 0 && g_styles[i].font < 254) usedFont[g_styles[i].font] = 1;
        for (int i = 0; i < 254; i++) if (usedFont[i]) g_fontList[g_nfont++] = (BYTE)i;
    }
    if (!g_nfont) for (int i = 0; i <= 4; i++) g_fontList[g_nfont++] = (BYTE)i;    /* stock LibertyCityPlates */

    plog("pictures: %d (%s, %d to %d), fonts: %d (%s, %d to %d)",
         g_npic, g_maxPic >= 0 ? "MaxPicture" : "from vehicles.ide", g_picList[0], g_picList[g_npic - 1],
         g_nfont, g_maxFont >= 0 ? "MaxFont" : "from vehicles.ide", g_fontList[0], g_fontList[g_nfont - 1]);
}

/* ------------------------------------------------------------ plate helpers */
static int to_plate_char(char c, BYTE *out)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= '0' && c <= '9') { *out = (BYTE)(c - '0'); return 1; }
    if (c >= 'A' && c <= 'Z') { *out = (BYTE)(c - 'A' + 10); return 1; }
    if (c == ' ') { *out = 0x25; return 1; }
    if (c == '*') { *out = 0x24; return 1; }
    return 0;
}

static void set_text(DWORD car, const char *text, int len)
{
    BYTE ch[12];
    for (int i = 0; i < 12; i++) ch[i] = 0x25;
    for (int i = 0; i < len && i < 12; i++) to_plate_char(text[i], &ch[i]);
    C5(LCP_TEXT_0123, car, ch[0], ch[1], ch[2], ch[3]);
    C5(LCP_TEXT_4567, car, ch[4], ch[5], ch[6], ch[7]);
    C5(LCP_TEXT_891011, car, ch[8], ch[9], ch[10], ch[11]);
    C2(LCP_TEXT_LENGTH, car, len);
}

/* picture + its preset text layout; returns the preset font or -1 */
static int set_picture(DWORD car, int pic)
{
    C2(LCP_PLATE_TEXTURE, car, pic);
    if (pic < 0 || pic >= 254 || !g_styles[pic].have) return -1;
    Style *s = &g_styles[pic];
    C3(LCP_TEXT_SCALE, car, fbits(s->sx), fbits(s->sy));
    C3(LCP_TEXT_OFFSET, car, fbits(s->ox), fbits(s->oy));
    C5(LCP_TEXT_TINT_FLOAT4, car, fbits(s->r), fbits(s->g), fbits(s->b), fbits(s->a));
    C2(LCP_TEXT_TEXTURE, car, s->font);
    return s->font;
}

typedef struct { DWORD model; int col[4]; float x, y, z; } CarId;

static void car_id(DWORD car, CarId *id)
{
    ZeroMemory(id, sizeof(*id));
    C2(GET_CAR_MODEL, car, &id->model);
    C3(GET_CAR_COLOURS, car, &id->col[0], &id->col[1]);
    C3(GET_EXTRA_CAR_COLOURS, car, &id->col[2], &id->col[3]);
    C4(GET_CAR_COORDINATES, car, &id->x, &id->y, &id->z);
}

/* ------------------------------------------------------------ remembered plates */
#define MAX_RECORDS 64
#define MAX_SPOTS   8
#define MATCH_DIST  6.0f    /* metres from a parked spot */
#define SPOT_DIST   3.0f    /* closer than this counts as the same spot */
#define PARK_MS     4000    /* standing still this long records a spot */

typedef struct {
    DWORD model;
    int col[4];
    char text[13];
    int len;               /* -1 = text not set */
    int pic, font;         /* -1 = not set */
    int nspots;
    int spot[MAX_SPOTS][3];   /* decimetres */
    /* runtime */
    DWORD car;
    DWORD stillSince;
    int parked;            /* this stop's spot is already recorded */
    int reapply;           /* the game paused or loaded: put the plate on again */
} Record;
static Record g_rec[MAX_RECORDS];
static int g_nrec;
static int g_dirty;

static int dist_ok(const int *spot, float x, float y, float z, float max)
{
    float dx = spot[0] / 10.0f - x, dy = spot[1] / 10.0f - y, dz = spot[2] / 10.0f - z;
    return dx * dx + dy * dy + dz * dz <= max * max;
}

static void add_spot(Record *r, float x, float y, float z)
{
    for (int i = 0; i < r->nspots; i++)
        if (dist_ok(r->spot[i], x, y, z, SPOT_DIST)) {
            /* already known: move it to the front */
            int keep[3] = { r->spot[i][0], r->spot[i][1], r->spot[i][2] };
            for (int j = i; j > 0; j--) { r->spot[j][0] = r->spot[j-1][0]; r->spot[j][1] = r->spot[j-1][1]; r->spot[j][2] = r->spot[j-1][2]; }
            r->spot[0][0] = keep[0]; r->spot[0][1] = keep[1]; r->spot[0][2] = keep[2];
            return;
        }
    int n = r->nspots < MAX_SPOTS ? r->nspots + 1 : MAX_SPOTS;
    for (int j = n - 1; j > 0; j--) { r->spot[j][0] = r->spot[j-1][0]; r->spot[j][1] = r->spot[j-1][1]; r->spot[j][2] = r->spot[j-1][2]; }
    r->spot[0][0] = (int)(x * 10); r->spot[0][1] = (int)(y * 10); r->spot[0][2] = (int)(z * 10);
    r->nspots = n;
    g_dirty = 1;
}

static void save_records(void)
{
    HANDLE h = CreateFileA(g_platesPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { plog("couldn't write %s (%lu)", g_platesPath, GetLastError()); return; }
    DWORD w;
    const char *head = "# PlateChanger remembered plates. One car per line:\r\n"
                       "# model colour1 colour2 colour3 colour4 picture font text spots x y z ...\r\n"
                       "# (-1 = not set, _ = space in the text, positions in decimetres)\r\n";
    WriteFile(h, head, lstrlenA(head), &w, NULL);
    for (int i = 0; i < g_nrec; i++) {
        Record *r = &g_rec[i];
        char text[16], line[512], part[48];
        if (r->len < 0) lstrcpyA(text, "-");
        else if (r->len == 0) lstrcpyA(text, "=");
        else { for (int k = 0; k < r->len; k++) text[k] = r->text[k] == ' ' ? '_' : r->text[k]; text[r->len] = 0; }
        wsprintfA(line, "%lu %d %d %d %d %d %d %s %d", r->model, r->col[0], r->col[1], r->col[2], r->col[3],
                  r->pic, r->font, text, r->nspots);
        for (int s = 0; s < r->nspots; s++) {
            wsprintfA(part, " %d %d %d", r->spot[s][0], r->spot[s][1], r->spot[s][2]);
            lstrcatA(line, part);
        }
        lstrcatA(line, "\r\n");
        WriteFile(h, line, lstrlenA(line), &w, NULL);
    }
    CloseHandle(h);
    g_dirty = 0;
}

static void load_records(void)
{
    char *buf = read_file(g_platesPath);
    g_nrec = 0;
    if (!buf) { plog("no remembered plates yet"); return; }
    char *line = buf;
    while (*line && g_nrec < MAX_RECORDS) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char save = *end; *end = 0;
        const char *p = line;
        char t[32];
        if (next_token(&p, t, sizeof(t)) && t[0] != '#') {
            Record r; ZeroMemory(&r, sizeof(r));
            int v, ok = parse_int(t, &v); r.model = (DWORD)v;
            for (int k = 0; k < 4 && ok; k++) ok = next_token(&p, t, sizeof(t)) && parse_int(t, &r.col[k]);
            ok = ok && next_token(&p, t, sizeof(t)) && parse_int(t, &r.pic);
            ok = ok && next_token(&p, t, sizeof(t)) && parse_int(t, &r.font);
            ok = ok && next_token(&p, t, sizeof(t));
            if (ok) {
                if (!lstrcmpA(t, "-")) r.len = -1;
                else if (!lstrcmpA(t, "=")) r.len = 0;
                else {
                    r.len = lstrlenA(t) > 12 ? 12 : lstrlenA(t);
                    for (int k = 0; k < r.len; k++) r.text[k] = t[k] == '_' ? ' ' : t[k];
                    r.text[r.len] = 0;
                }
            }
            ok = ok && next_token(&p, t, sizeof(t)) && parse_int(t, &r.nspots) && r.nspots >= 0 && r.nspots <= MAX_SPOTS;
            for (int s = 0; s < r.nspots && ok; s++)
                for (int k = 0; k < 3 && ok; k++) ok = next_token(&p, t, sizeof(t)) && parse_int(t, &r.spot[s][k]);
            if (ok) g_rec[g_nrec++] = r;
        }
        *end = save;
        line = *end ? end + 1 : end;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    plog("remembered plates: %d car(s)", g_nrec);
}

static Record *record_for_car(DWORD car)
{
    for (int i = 0; i < g_nrec; i++) if (g_rec[i].car == car) return &g_rec[i];
    return NULL;
}

static int same_car(const Record *r, const CarId *id)
{
    return r->model == id->model && r->col[0] == id->col[0] && r->col[1] == id->col[1] &&
           r->col[2] == id->col[2] && r->col[3] == id->col[3];
}

static void apply_record(Record *r, DWORD car)
{
    if (r->pic >= 0) set_picture(car, r->pic);        /* also sets the preset font */
    if (r->font >= 0) C2(LCP_TEXT_TEXTURE, car, r->font);
    if (r->len >= 0) set_text(car, r->text, r->len);
}

/* ------------------------------------------------------------ watching for saved cars */
static DWORD g_slotHandle[4096];

static int g_wasPaused;

static void watch_vehicles(void)
{
    if (!g_remember) return;
    if (game_paused()) {
        /* loading a save can re-create cars with the same handles they had before,
           so after any pause or load look at every car again */
        if (!g_wasPaused) {
            for (DWORD i = 0; i < 4096; i++) g_slotHandle[i] = 0;
            for (int i = 0; i < g_nrec; i++) if (g_rec[i].car) g_rec[i].reapply = 1;
        }
        g_wasPaused = 1;
        return;
    }
    g_wasPaused = 0;
    Pool *pool = veh_pool();
    if (!pool) return;
    DWORD now = GetTickCount();

    /* 1. cars we've given a plate: keep track of where they stand parked */
    for (int i = 0; i < g_nrec; i++) {
        Record *r = &g_rec[i];
        if (!r->car) continue;
        DWORD slot = r->car >> 8;
        if (slot >= pool->size || (pool->flags[slot] & 0x80) || ((slot << 8) | pool->flags[slot]) != r->car) { r->car = 0; continue; }
        CarId id; car_id(r->car, &id);
        if (!same_car(r, &id)) { r->car = 0; continue; }     /* resprayed or a different car */
        if (r->reapply) { r->reapply = 0; apply_record(r, r->car); }
        float speed = 1;
        C2(GET_CAR_SPEED, r->car, &speed);
        if (speed < 0.3f) {
            if (!r->stillSince) r->stillSince = now;
            else if (!r->parked && now - r->stillSince > PARK_MS) { add_spot(r, id.x, id.y, id.z); r->parked = 1; }
        } else { r->stillSince = 0; r->parked = 0; }
    }

    /* 2. newly created cars: is one of them a remembered car coming back? */
    for (DWORD slot = 0; slot < pool->size; slot++) {
        BYTE f = pool->flags[slot];
        if (f & 0x80) { g_slotHandle[slot] = 0; continue; }
        DWORD car = (slot << 8) | f;
        if (g_slotHandle[slot] == car) continue;
        g_slotHandle[slot] = car;
        if (record_for_car(car)) continue;
        CarId id; car_id(car, &id);
        for (int i = 0; i < g_nrec; i++) {
            Record *r = &g_rec[i];
            if (r->car || !same_car(r, &id)) continue;
            int closeBy = 0;
            for (int sp = 0; sp < r->nspots && !closeBy; sp++) closeBy = dist_ok(r->spot[sp], id.x, id.y, id.z, MATCH_DIST);
            if (!closeBy) continue;
            r->car = car; r->stillSince = 0; r->parked = 1; r->reapply = 0;
            apply_record(r, car);
            plog("put the remembered plate back on a car (model %lu)", r->model);
            break;
        }
    }
    if (g_dirty) save_records();
}

/* ------------------------------------------------------------ in-game editing */
static HWND g_game;
static WNDPROC g_origProc;
static int g_unicode;

static int g_editing;            /* typing a plate right now */
static DWORD g_editCar, g_editPlayer;
static int g_controlWasOn;
static int g_restorePending;     /* give controls back once Enter / the hotkey is released */
static char g_text[13];
static int g_len;
static int g_textTouched;
static int g_pic, g_font;        /* -1 = not changed (the current value can't be read) */
static BYTE g_keyPrev[256];
static DWORD g_lastShown;

static void show_edit(void)
{
    char line[160], pic[8], font[8];
    if (g_pic < 0) lstrcpyA(pic, "-"); else wsprintfA(pic, "%d", g_pic);
    if (g_font < 0) lstrcpyA(font, "-"); else wsprintfA(font, "%d", g_font);
    wsprintfA(line, "Plate: %s_   Picture %s (PgUp/PgDn)   Font %s (Home/End)   Enter = done",
              g_textTouched ? g_text : "(unchanged)", pic, font);
    lstrcpynA(g_msg, line, sizeof(g_msg));
    C4(PRINT_STRING_WITH_LITERAL_STRING_NOW, g_gxtString, g_msg, 60000, 1);
    g_lastShown = GetTickCount();
}

static void remember_edit(void)
{
    if (!g_remember) return;
    CarId id; car_id(g_editCar, &id);
    Record *r = record_for_car(g_editCar);
    if (!r) {
        if (g_nrec == MAX_RECORDS) {               /* full: forget the oldest */
            for (int i = 1; i < g_nrec; i++) g_rec[i - 1] = g_rec[i];
            g_nrec--;
        }
        r = &g_rec[g_nrec++];
        ZeroMemory(r, sizeof(*r));
        r->len = -1; r->pic = -1; r->font = -1;
    }
    r->model = id.model;
    for (int k = 0; k < 4; k++) r->col[k] = id.col[k];
    if (g_textTouched) { lstrcpyA(r->text, g_text); r->len = g_len; }
    if (g_pic >= 0) r->pic = g_pic;
    if (g_font >= 0) r->font = g_font;
    r->car = g_editCar;
    r->reapply = 0;
    add_spot(r, id.x, id.y, id.z);
    save_records();
}

static void end_edit(void)
{
    g_editing = 0;
    if (g_controlWasOn) g_restorePending = 1;
    if (!g_textTouched && g_pic < 0 && g_font < 0) { show("Plate not changed."); plog("nothing changed"); return; }
    if (C1(DOES_VEHICLE_EXIST, g_editCar)) remember_edit();
    char done[96], part[32];
    lstrcpyA(done, "Plate saved");
    if (g_textTouched) { wsprintfA(part, ": %s", g_text); lstrcatA(done, part); }
    if (g_pic >= 0) { wsprintfA(part, ", picture %d", g_pic); lstrcatA(done, part); }
    if (g_font >= 0) { wsprintfA(part, ", font %d", g_font); lstrcatA(done, part); }
    show(done);
    plog("%s", done);
}

static void restore_controls_when_ready(void)
{
    if (!g_restorePending || game_paused()) return;
    if ((GetAsyncKeyState(VK_RETURN) & 0x8000) || (GetAsyncKeyState((int)g_vk) & 0x8000)) return;
    g_restorePending = 0;
    C2(SET_PLAYER_CONTROL, g_editPlayer, 1);
}

static void start_edit(void)
{
    if (g_restorePending) return;
    if (game_paused()) { plog("hotkey ignored: game is paused or loading"); return; }
    g_missing = 0;
    DWORD player = C1(CONVERT_INT_TO_PLAYERINDEX, C0(GET_PLAYER_ID));
    if (g_missing) return;
    if (!C1(IS_PLAYER_PLAYING, player)) { plog("hotkey ignored: player not playing"); return; }
    if (!get_handler(LCP_TEXT_0123)) {
        show("PlateChanger: LibertyCityPlates isn't loaded.");
        plog("LibertyCityPlates commands not found");
        return;
    }
    DWORD ped = 0, car = 0;
    C2(GET_PLAYER_CHAR, player, &ped);
    if (ped && C1(IS_CHAR_IN_ANY_CAR, ped)) C2(GET_CAR_CHAR_IS_USING, ped, &car);
    if (!car) C1(GET_PLAYERS_LAST_CAR_NO_SAVE, &car);
    if (!car || !C1(DOES_VEHICLE_EXIST, car)) { show("PlateChanger: no car to change. Get in one first."); return; }
    plog("changing plate of vehicle handle %lu", car);

    g_editCar = car; g_editPlayer = player;
    g_text[0] = 0; g_len = 0; g_textTouched = 0;
    g_pic = -1; g_font = -1;
    Record *r = record_for_car(car);             /* a remembered car: show what it has */
    if (r) {
        if (r->len >= 0) { lstrcpyA(g_text, r->text); g_len = r->len; g_textTouched = 1; }
        g_pic = r->pic; g_font = r->font;
    }
    g_controlWasOn = C1(IS_PLAYER_CONTROL_ON, player) ? 1 : 0;
    if (g_controlWasOn) C2(SET_PLAYER_CONTROL, player, 0);   /* typing shouldn't drive the car */
    for (int k = 0; k < 256; k++) g_keyPrev[k] = (GetAsyncKeyState(k) & 0x8000) ? 1 : 0;
    g_editing = 1;
    show_edit();
}

static int pressed(int vk)
{
    int down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    int was = g_keyPrev[vk];
    g_keyPrev[vk] = (BYTE)down;
    return down && !was;
}

static int step(int value, int dir, const BYTE *list, int n)
{
    int i = -1;
    for (int k = 0; k < n; k++) if (list[k] == value) { i = k; break; }
    if (i < 0) {                                       /* not set yet, or not in the list */
        if (value < 0) return dir > 0 ? list[0] : list[n - 1];
        for (i = 0; i < n && list[i] < value; i++) {}   /* nearest in the direction pressed */
        if (dir > 0) return i < n ? list[i] : list[0];
        return i > 0 ? list[i - 1] : list[n - 1];
    }
    i = (i + dir + n) % n;
    return list[i];
}

static void poll_edit(void)
{
    if (game_paused()) { end_edit(); return; }
    if (!C1(DOES_VEHICLE_EXIST, g_editCar)) { g_editing = 0; if (g_controlWasOn) g_restorePending = 1; show("The car is gone."); return; }
    int changed = GetTickCount() - g_lastShown > 15000;   /* keep the prompt on screen */
    if (pressed(VK_RETURN) || pressed((int)g_vk)) { end_edit(); return; }

    int textChanged = 0;
    if (pressed(VK_BACK) && g_len > 0) { g_text[--g_len] = 0; textChanged = 1; }
    char add = 0;
    for (int k = 'A'; k <= 'Z' && !add; k++) if (pressed(k)) add = (char)k;
    for (int k = '0'; k <= '9' && !add; k++) if (pressed(k)) add = (char)k;
    for (int k = 0; k <= 9 && !add; k++) if (pressed(VK_NUMPAD0 + k)) add = (char)('0' + k);
    if (!add && pressed(VK_SPACE)) add = ' ';
    if (!add && pressed(VK_MULTIPLY)) add = '*';
    if (add && g_len < 12) { g_text[g_len++] = add; g_text[g_len] = 0; textChanged = 1; }
    if (textChanged) { g_textTouched = 1; set_text(g_editCar, g_text, g_len); changed = 1; }

    int picDir = pressed(VK_PRIOR) ? 1 : pressed(VK_NEXT) ? -1 : 0;     /* Page Up / Page Down */
    if (picDir) {
        g_pic = step(g_pic, picDir, g_picList, g_npic);
        set_picture(g_editCar, g_pic);                                     /* with its preset layout and font */
        if (g_font >= 0) C2(LCP_TEXT_TEXTURE, g_editCar, g_font);        /* a font you picked wins */
        changed = 1;
    }
    int fontDir = pressed(VK_HOME) ? 1 : pressed(VK_END) ? -1 : 0;
    if (fontDir) { g_font = step(g_font, fontDir, g_fontList, g_nfont); C2(LCP_TEXT_TEXTURE, g_editCar, g_font); changed = 1; }

    if (changed) show_edit();
}

/* ------------------------------------------------------------ window hook */
static int g_busy;
static int g_held;        /* hotkey is down and has already fired */
static int g_timerSet;
static int g_tick;
#define TIMER_ID 0x504C   /* 'PL' */

static int mods_ok(int async)
{
    int shift, ctrl, alt;
    if (async) {
        shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    } else {
        shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    }
    return shift == g_needShift && ctrl == g_needCtrl && alt == g_needAlt;
}

static void fire(void)
{
    g_held = 1;
    g_busy = 1;
    start_edit();
    g_busy = 0;
}

static LRESULT CALLBACK HookProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    /* the timer has to be created from the game's own thread, so do it here */
    if (!g_timerSet) {
        g_timerSet = 1;
        if (SetTimer(h, TIMER_ID, 30, NULL)) plog("key polling started");
        else plog("couldn't start key polling (%lu); using key messages only", GetLastError());
    }
    if (m == WM_TIMER && w == TIMER_ID) {
        if (g_editing) { poll_edit(); g_held = 1; return 0; }
        restore_controls_when_ready();
        if (++g_tick % 8 == 0) watch_vehicles();                /* about 4 times a second */
        int down = (GetAsyncKeyState((int)g_vk) & 0x8000) != 0;
        if (!down) g_held = 0;
        else if (!g_held && !g_busy && GetForegroundWindow() == h && mods_ok(1)) fire();
        return 0;
    }
    if (g_editing && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_CHAR || m == WM_KEYUP)) return 0;
    if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && w == g_vk) {
        if (!g_held && !g_busy && mods_ok(0)) { fire(); return 0; }
        if (g_held) return 0;
    }
    if ((m == WM_KEYUP || m == WM_SYSKEYUP) && w == g_vk) g_held = 0;
    return g_unicode ? CallWindowProcW(g_origProc, h, m, w, l) : CallWindowProcA(g_origProc, h, m, w, l);
}

static BOOL CALLBACK find_window(HWND h, LPARAM lp)
{
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    char cls[64];
    GetClassNameA(h, cls, sizeof(cls));
    RECT r; GetClientRect(h, &r);
    if (r.right - r.left < 320 || r.bottom - r.top < 200) return TRUE;
    *(HWND *)lp = h;
    return lstrcmpA(cls, "grcWindow") != 0;   /* stop at the game's own window class */
}

static DWORD WINAPI init_thread(LPVOID unused)
{
    (void)unused;
    load_settings();
    load_styles();
    build_lists();
    load_records();

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)dos + dos->e_lfanew);
    g_base = (BYTE *)dos;
    g_size = nt->OptionalHeader.SizeOfImage;

    BYTE *p;
    int cnt = 0;
    /* same lookups FusionFix does, in the same order */
    p = find_pattern_n("56 8B 35 ? ? ? ? 85 F6 75 06", 1, &cnt);
    plog("lookup function pattern: %d match(es)", cnt);
    if (cnt == 2 && p) g_getNativeAddress = (GetNativeAddressFn)p;
    if ((p = find_pattern("8B 35 ? ? ? ? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10")) ||
        (p = find_pattern("8B 3D ? ? ? ? 85 FF 75 04 33 C0 5F C3")))
        g_pTableSize = *(DWORD **)(p + 2);
    if ((p = find_pattern("8B 1D ? ? ? ? 8B CF 8B 04 D3 3B C7 74 19 8D 64 24 00 85 C0")) ||
        (p = find_pattern("8B 1D ? ? ? ? 8B CE 8B 04 D3 3B C6 74 17 85 C0")))
        g_ppTable = *(DWORD ***)(p + 2);
    if ((p = find_pattern("0F B6 0D ? ? ? ? 0F B6 C0 0B C1"))) {
        g_pUserPause = *(BYTE **)(p + 3); g_pScriptPause = *(BYTE **)(p + 15);
    } else if ((p = find_pattern("0F B6 0D ? ? ? ? 0F B6 15 ? ? ? ? 33 C0 0B C1"))) {
        g_pUserPause = *(BYTE **)(p + 3); g_pScriptPause = *(BYTE **)(p + 10);
    }
    /* vehicle pool: Complete Edition pattern first, then 1.0.x / 1.1.x (as LibertyCityPlates does) */
    if ((p = find_pattern("8B 0D ? ? ? ? F3 0F 11 05 ? ? ? ? F3 0F 10 00 F3 0F 11 05 ? ? ? ? F3 0F 11 0D ? ? ? ? F3 0F 11 15 ? ? ? ? F3 0F 10 40 ? 8A 44 24 0C A2 ? ? ? ? 8A 44 24 10 A2 ? ? ? ? 8A 44 24 14")) ||
        (p = find_pattern("8B 0D ? ? ? ? 68 ? ? ? ? F3 0F 11 05 ? ? ? ? A2 ? ? ? ? C6 05 ? ? ? ? ? 88 15 ? ? ? ? E8 ? ? ? ? C3")))
        g_ppVehPool = *(DWORD **)(p + 2);
    plog("lookup function %p, command table: size var %p, table var %p, pause flags %p %p, vehicle pool var %p",
         g_getNativeAddress, g_pTableSize, g_ppTable, g_pUserPause, g_pScriptPause, g_ppVehPool);
    if (!g_getNativeAddress && (!g_pTableSize || !g_ppTable)) { plog("ERROR: this game version isn't supported (command table not found)"); return 0; }
    if (!g_ppVehPool) plog("vehicle pool not found: plates can be changed but won't be remembered");

    HWND w = NULL;
    for (int tries = 0; tries < 1200 && !w; tries++) {   /* up to 10 minutes */
        EnumWindows(find_window, (LPARAM)&w);
        if (!w) Sleep(500);
    }
    if (!w) { plog("ERROR: game window not found"); return 0; }
    char cls[64]; GetClassNameA(w, cls, sizeof(cls));
    g_game = w;
    g_unicode = IsWindowUnicode(w);
    g_origProc = (WNDPROC)(g_unicode ? SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)HookProc)
                                     : SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)HookProc));
    plog("hooked game window (class %s, %s)", cls, g_unicode ? "unicode" : "ansi");
    for (int tries = 0; tries < 600; tries++) {   /* the table is filled while the game starts */
        if (get_handler(GET_PLAYER_ID)) break;
        Sleep(500);
    }
    plog("self-test: GET_PLAYER_ID %s, LibertyCityPlates commands %s, vehicle pool %s",
         get_handler(GET_PLAYER_ID) ? "found" : "NOT found", get_handler(LCP_TEXT_0123) ? "found" : "NOT found",
         veh_pool() ? "ok" : "not ready yet");
    plog("ready");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        char *slash = g_dir;
        for (char *s = g_dir; *s; s++) if (*s == '\\' || *s == '/') slash = s;
        *slash = 0;
        wsprintfA(g_logPath, "%s\\PlateChanger.log", g_dir);
        wsprintfA(g_iniPath, "%s\\PlateChanger.ini", g_dir);
        wsprintfA(g_platesPath, "%s\\PlateChanger.plates", g_dir);
        DeleteFileA(g_logPath);
        plog("PlateChanger " VERSION " loaded");
        HANDLE t = CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

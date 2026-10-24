/*
 * PlateChanger.asi - change the LibertyCityPlates plate of the car you're in
 * (or the last car you drove) with a hotkey. For GTA IV / EFLC, including
 * Complete Edition 1.2.0.59 with FusionFix.
 *
 * How it works
 *   - Finds the game's script command table (same byte patterns FusionFix
 *     uses for 1.0.x and 1.2.x) and calls commands by hash.
 *   - Hooks the game window's message handler. The hotkey is handled there,
 *     on the game's main thread between frames, where FusionFix also calls
 *     script commands from.
 *   - The new plate is typed in-game: it shows in the subtitle line while
 *     you type, and your controls are paused until you press Enter (save)
 *     or the hotkey again (cancel).
 *   - Uses LibertyCityPlates' own script commands to write the plate.
 *
 * Settings: PlateChanger.ini next to this file. Log: PlateChanger.log.
 * Plain Win32, no C runtime needed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* no C runtime: the compiler may still emit calls to these */
void *memset(void *d, int c, unsigned int n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned int n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }

/* ------------------------------------------------------------ logging */
static char g_dir[MAX_PATH];
static char g_logPath[MAX_PATH];
static char g_iniPath[MAX_PATH];

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
    BYTE bytes[64]; BYTE mask[64]; int n = 0;
    for (const char *p = pat; *p && n < 64;) {
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

/* call a command with up to 8 32-bit arguments; returns the first result */
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
#define PRINT_STRING_WITH_LITERAL_STRING_NOW 0x0CA539D6
#define SET_PLAYER_CONTROL                   0x1A6203EA
#define IS_PLAYER_CONTROL_ON                 0x30CD2F1F
/* LibertyCityPlates commands (registered by LibertyCityPlates.asi) */
#define LCP_TEXT_0123                        0x09DED943
#define LCP_TEXT_4567                        0x09DE9587
#define LCP_TEXT_891011                      0x0E515A91
#define LCP_TEXT_LENGTH                      0x46F3540C
#define LCP_TEXT_TEXTURE                     0xF055490B  /* font */
#define LCP_PLATE_TEXTURE                    0xC621B628  /* plate picture */

static char g_msg[160];
static char g_gxtString[] = "STRING";
static void show(const char *text)
{
    lstrcpynA(g_msg, text, sizeof(g_msg));
    C4(PRINT_STRING_WITH_LITERAL_STRING_NOW, g_gxtString, g_msg, 4000, 1);
}

/* ------------------------------------------------------------ settings */
static UINT g_vk = VK_F11;
static int g_needShift, g_needCtrl, g_needAlt;

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
    plog("hotkey: %s%s%svk 0x%02X", g_needCtrl ? "Ctrl+" : "", g_needAlt ? "Alt+" : "", g_needShift ? "Shift+" : "", g_vk);
}

/* ------------------------------------------------------------ in-game editing */
static HWND g_game;
static WNDPROC g_origProc;
static int g_unicode;

static int g_editing;            /* typing a plate right now */
static DWORD g_editCar, g_editPlayer;
static int g_controlWasOn;
static int g_restorePending;    /* give controls back once Enter / the hotkey is released */
static char g_text[13];
static int g_len;
static BYTE g_keyPrev[256];
static DWORD g_lastShown;

static int to_plate_char(char c, BYTE *out)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= '0' && c <= '9') { *out = (BYTE)(c - '0'); return 1; }
    if (c >= 'A' && c <= 'Z') { *out = (BYTE)(c - 'A' + 10); return 1; }
    if (c == ' ') { *out = 0x25; return 1; }
    if (c == '*') { *out = 0x24; return 1; }
    return 0;
}

static void show_edit(void)
{
    char line[128];
    wsprintfA(line, "New plate: %s_    Enter = save, F11 = cancel", g_text);
    lstrcpynA(g_msg, line, sizeof(g_msg));
    C4(PRINT_STRING_WITH_LITERAL_STRING_NOW, g_gxtString, g_msg, 60000, 1);
    g_lastShown = GetTickCount();
}

static void end_edit(int save)
{
    g_editing = 0;
    if (g_controlWasOn) g_restorePending = 1;
    if (!save) { show("Plate not changed."); plog("cancelled"); return; }
    if (g_len == 0) { show("Plate not changed (nothing typed)."); return; }
    if (!C1(DOES_VEHICLE_EXIST, g_editCar)) { show("The car is gone, plate not changed."); return; }
    BYTE ch[12];
    for (int i = 0; i < 12; i++) ch[i] = 0x25;
    for (int i = 0; i < g_len; i++) to_plate_char(g_text[i], &ch[i]);
    C5(LCP_TEXT_0123, g_editCar, ch[0], ch[1], ch[2], ch[3]);
    C5(LCP_TEXT_4567, g_editCar, ch[4], ch[5], ch[6], ch[7]);
    C5(LCP_TEXT_891011, g_editCar, ch[8], ch[9], ch[10], ch[11]);
    C2(LCP_TEXT_LENGTH, g_editCar, g_len);
    char done[64];
    wsprintfA(done, "Plate changed to %s", g_text);
    show(done);
    plog("%s", done);
}

static int game_paused(void)
{
    return (g_pUserPause && *g_pUserPause) || (g_pScriptPause && *g_pScriptPause);
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
    g_text[0] = 0; g_len = 0;
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

static void poll_edit(void)
{
    if (game_paused()) { end_edit(0); return; }
    int changed = GetTickCount() - g_lastShown > 15000;   /* keep the prompt on screen */
    if (pressed(VK_RETURN)) { end_edit(1); return; }
    if (pressed((int)g_vk)) { end_edit(0); return; }
    if (pressed(VK_BACK) && g_len > 0) { g_text[--g_len] = 0; changed = 1; }
    char add = 0;
    for (int k = 'A'; k <= 'Z' && !add; k++) if (pressed(k)) add = (char)k;
    for (int k = '0'; k <= '9' && !add; k++) if (pressed(k)) add = (char)k;
    for (int k = 0; k <= 9 && !add; k++) if (pressed(VK_NUMPAD0 + k)) add = (char)('0' + k);
    if (!add && pressed(VK_SPACE)) add = ' ';
    if (!add && pressed(VK_MULTIPLY)) add = '*';
    if (add && g_len < 12) { g_text[g_len++] = add; g_text[g_len] = 0; changed = 1; }
    if (changed) show_edit();
}

/* ------------------------------------------------------------ window hook */
static int g_busy;
static int g_held;        /* hotkey is down and has already fired */
static int g_timerSet;
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
    if (!lstrcmpA(cls, "PlateChangerDlg")) return TRUE;
    RECT r; GetClientRect(h, &r);
    if (r.right - r.left < 320 || r.bottom - r.top < 200) return TRUE;
    *(HWND *)lp = h;
    return lstrcmpA(cls, "grcWindow") != 0;   /* stop at the game's own window class */
}

static DWORD WINAPI init_thread(LPVOID unused)
{
    (void)unused;
    load_settings();

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
    plog("lookup function %p, command table: size var %p, table var %p, pause flags %p %p",
         g_getNativeAddress, g_pTableSize, g_ppTable, g_pUserPause, g_pScriptPause);
    if (!g_getNativeAddress && (!g_pTableSize || !g_ppTable)) { plog("ERROR: this game version isn't supported (command table not found)"); return 0; }

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
    plog("self-test: GET_PLAYER_ID %s, LibertyCityPlates commands %s",
         get_handler(GET_PLAYER_ID) ? "found" : "NOT found", get_handler(LCP_TEXT_0123) ? "found" : "NOT found");
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
        DeleteFileA(g_logPath);
        plog("PlateChanger 1.1 loaded");
        HANDLE t = CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

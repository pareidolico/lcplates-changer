/* just enough of windows.h to run PlateChanger's file code on Linux */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <strings.h>
typedef uint32_t DWORD; typedef uint8_t BYTE; typedef int BOOL; typedef unsigned int UINT;
typedef void *HANDLE, *HWND, *HINSTANCE, *HMODULE, *LPVOID; typedef intptr_t LONG_PTR, LRESULT, LPARAM; typedef uintptr_t WPARAM;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (*WNDENUMPROC)(HWND, LPARAM);
typedef struct { long left, top, right, bottom; } RECT;
typedef struct { uint16_t e_magic; char pad[58]; int32_t e_lfanew; } IMAGE_DOS_HEADER;
typedef struct { DWORD SizeOfImage; } IMAGE_OPTIONAL_HEADER;
typedef struct { DWORD sig; IMAGE_OPTIONAL_HEADER OptionalHeader; } IMAGE_NT_HEADERS;
#define WINAPI
#define CALLBACK
#define __stdcall
#define __cdecl
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_FILE_SIZE 0xFFFFFFFF
#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define FILE_APPEND_DATA 4
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define VK_F1 0x70
#define VK_F11 0x7A
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_RETURN 0x0D
#define VK_BACK 0x08
#define VK_SPACE 0x20
#define VK_MULTIPLY 0x6A
#define VK_NUMPAD0 0x60
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_HOME 0x24
#define VK_END 0x23
#define WM_TIMER 0x113
#define WM_KEYDOWN 0x100
#define WM_KEYUP 0x101
#define WM_CHAR 0x102
#define WM_SYSKEYDOWN 0x104
#define WM_SYSKEYUP 0x105
#define GWLP_WNDPROC (-4)
#define GW_OWNER 4
#define DLL_PROCESS_ATTACH 1
static inline HANDLE CreateFileA(const char *p, DWORD acc, DWORD sh, void *sa, DWORD disp, DWORD fl, void *t) {
    const char *mode = disp == OPEN_EXISTING ? "rb" : disp == CREATE_ALWAYS ? "wb" : "ab";
    FILE *f = fopen(p, mode); return f ? (HANDLE)f : INVALID_HANDLE_VALUE; }
static inline DWORD GetFileSize(HANDLE h, DWORD *hi) { FILE *f = h; long c = ftell(f); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, c, SEEK_SET); return (DWORD)n; }
static inline BOOL ReadFile(HANDLE h, void *b, DWORD n, DWORD *got, void *o) { *got = (DWORD)fread(b, 1, n, h); return 1; }
static inline BOOL WriteFile(HANDLE h, const void *b, DWORD n, DWORD *w, void *o) { *w = (DWORD)fwrite(b, 1, n, h); return 1; }
static inline BOOL CloseHandle(HANDLE h) { fclose(h); return 1; }
static inline HANDLE GetProcessHeap(void) { return 0; }
static inline void *HeapAlloc(HANDLE h, DWORD f, size_t n) { return malloc(n); }
static inline BOOL HeapFree(HANDLE h, DWORD f, void *p) { free(p); return 1; }
#define lstrcpyA strcpy
#define lstrcatA strcat
#define lstrlenA (int)strlen
#define lstrcmpA strcmp
#define lstrcmpiA strcasecmp
static inline char *lstrcpynA(char *d, const char *s, int n) { strncpy(d, s, n - 1); d[n - 1] = 0; return d; }
#define wsprintfA sprintf
#define wvsprintfA vsprintf
static char g_fakeExe[MAX_PATH] = "/tmp/game/GTAIV.exe";
static inline DWORD GetModuleFileNameA(void *m, char *b, DWORD n) { strcpy(b, g_fakeExe); return (DWORD)strlen(b); }
static inline DWORD GetPrivateProfileIntA(const char *a, const char *k, int d, const char *f) { return d; }
static inline DWORD GetPrivateProfileStringA(const char *a, const char *k, const char *d, char *o, DWORD n, const char *f) { strcpy(o, d); return 0; }
static inline char *CharUpperA(char *s) { for (char *p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; return s; }
static inline DWORD GetLastError(void) { return 0; }
static inline DWORD GetTickCount(void) { return 0; }
static inline short GetAsyncKeyState(int k) { return 0; }
static inline short GetKeyState(int k) { return 0; }
static inline HWND GetForegroundWindow(void) { return 0; }
static inline uintptr_t SetTimer(HWND h, uintptr_t id, UINT ms, void *f) { return 1; }
static inline LRESULT CallWindowProcA(WNDPROC p, HWND h, UINT m, WPARAM w, LPARAM l) { return 0; }
static inline LRESULT CallWindowProcW(WNDPROC p, HWND h, UINT m, WPARAM w, LPARAM l) { return 0; }
static inline DWORD GetWindowThreadProcessId(HWND h, DWORD *p) { *p = 0; return 0; }
static inline DWORD GetCurrentProcessId(void) { return 1; }
static inline BOOL IsWindowVisible(HWND h) { return 0; }
static inline HWND GetWindow(HWND h, UINT c) { return 0; }
static inline int GetClassNameA(HWND h, char *b, int n) { b[0] = 0; return 0; }
static inline BOOL GetClientRect(HWND h, RECT *r) { return 0; }
static inline BOOL EnumWindows(WNDENUMPROC f, LPARAM l) { return 0; }
static inline void Sleep(DWORD ms) {}
static inline BOOL IsWindowUnicode(HWND h) { return 0; }
static inline LONG_PTR SetWindowLongPtrA(HWND h, int i, LONG_PTR v) { return 0; }
static inline LONG_PTR SetWindowLongPtrW(HWND h, int i, LONG_PTR v) { return 0; }
static inline HMODULE GetModuleHandleA(const char *n) { return 0; }
static inline BOOL DisableThreadLibraryCalls(HMODULE m) { return 1; }
static inline BOOL DeleteFileA(const char *p) { return remove(p) == 0; }
static inline HANDLE CreateThread(void *a, size_t s, DWORD (*f)(LPVOID), void *p, DWORD fl, DWORD *id) { return 0; }
#define ZeroMemory(p, n) memset((p), 0, (n))

/*
 * Native Windows tray helper – JSON-lines stdin/stdout protocol.
 * Build (MSVC): 
 * cl /O2 /DUNICODE /D_UNICODE main.c cJSON.c /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib gdi32.lib kernel32.lib
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

#include "cJSON.h"

/* -----------------------------------------------------------------------
 * Constants & Macros
 * ----------------------------------------------------------------------- */
#define WM_TRAYICON     (WM_USER + 1)
#define WM_STDIN_CMD    (WM_USER + 2)
#define MAX_MENU_ITEMS  4096
#define MAX_TOOLTIP     128

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */
static HWND             gHwnd;
static NOTIFYICONDATAW  gNid;
static HMENU            gMenu;
static HICON            gIcon;
static UINT             gTaskbarCreatedMsg;
static CRITICAL_SECTION gOutputLock;
static HANDLE           gStdinHandle;
static HANDLE           gStdoutHandle;

static char             gInitialIcon[MAX_PATH];
static WCHAR            gInitialTooltip[MAX_TOOLTIP];

typedef struct {
    UINT    cmdId;
    char    id[256];
} MenuIdEntry;

static MenuIdEntry gMenuIdMap[MAX_MENU_ITEMS];
static UINT        gMenuIdCount;
static UINT        gNextCmdId = 1;

/* -----------------------------------------------------------------------
 * JSON output
 * ----------------------------------------------------------------------- */
static void emit(const char *method, cJSON *params) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "method", method);
    if (params) cJSON_AddItemToObject(msg, "params", params);
    
    char *str = cJSON_PrintUnformatted(msg);
    if (str) {
        EnterCriticalSection(&gOutputLock);
        DWORD written;
        WriteFile(gStdoutHandle, str, (DWORD)strlen(str), &written, NULL);
        WriteFile(gStdoutHandle, "\n", 1, &written, NULL);
        FlushFileBuffers(gStdoutHandle);
        LeaveCriticalSection(&gOutputLock);
        free(str);
    }
    cJSON_Delete(msg);
}

/* -----------------------------------------------------------------------
 * Icon Logic
 * ----------------------------------------------------------------------- */
static HICON createDefaultIcon(void) {
    int sz = GetSystemMetrics(SM_CXSMICON); // Scale for DPI
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz;
    bmi.bmiHeader.biHeight = -sz;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hColor = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (bits) {
        BYTE *px = (BYTE *)bits;
        float half = sz / 2.0f;
        float r2 = (half - 1) * (half - 1);
        for (int y = 0; y < sz; y++) {
            for (int x = 0; x < sz; x++) {
                float dx = x - half + 0.5f;
                float dy = y - half + 0.5f;
                int off = (y * sz + x) * 4;
                if (dx*dx + dy*dy <= r2) {
                    px[off+0] = 0x33; px[off+1] = 0xad; px[off+2] = 0x2e; px[off+3] = 0xff;
                }
            }
        }
    }
    HBITMAP hMask = CreateBitmap(sz, sz, 1, 1, NULL);
    ICONINFO ii = {TRUE, 0, 0, hMask, hColor};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(hMask);
    DeleteObject(hColor);
    return icon;
}

static unsigned char b64_rev[256];
static void initB64(void) {
    memset(b64_rev, 0x40, 256);
    for (int i=0; i<64; i++) b64_rev["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
}

static unsigned char *base64Decode(const char *src, size_t *outLen) {
    size_t len = strlen(src);
    if (len % 4 != 0) return NULL;
    size_t dLen = (len / 4) * 3;
    if (src[len-1] == '=') dLen--;
    if (src[len-2] == '=') dLen--;
    unsigned char *out = malloc(dLen);
    for (size_t i=0, j=0; i<len; ) {
        unsigned int a = b64_rev[(unsigned char)src[i++]];
        unsigned int b = b64_rev[(unsigned char)src[i++]];
        unsigned int c = b64_rev[(unsigned char)src[i++]];
        unsigned int d = b64_rev[(unsigned char)src[i++]];
        unsigned int triple = (a << 18) | (b << 12) | (c << 6) | d;
        if (j < dLen) out[j++] = (triple >> 16) & 0xFF;
        if (j < dLen) out[j++] = (triple >> 8) & 0xFF;
        if (j < dLen) out[j++] = triple & 0xFF;
    }
    *outLen = dLen;
    return out;
}

/* -----------------------------------------------------------------------
 * Command Handlers
 * ----------------------------------------------------------------------- */
static void buildMenuItems(HMENU menu, cJSON *items) {
    int count = cJSON_GetArraySize(items);
    for (int i = 0; i < count; i++) {
        cJSON *cfg = cJSON_GetArrayItem(items, i);
        if (cJSON_IsTrue(cJSON_GetObjectItem(cfg, "separator"))) {
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        const char *title_val = cJSON_GetStringValue(cJSON_GetObjectItem(cfg, "title"));
        const char *title = title_val ? title_val : "";
        const char *id_val = cJSON_GetStringValue(cJSON_GetObjectItem(cfg, "id"));
        const char *itemId = id_val ? id_val : "";
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
        WCHAR *wtitle = malloc(wlen * sizeof(WCHAR));
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, wlen);

        cJSON *jChildren = cJSON_GetObjectItem(cfg, "items");
        if (cJSON_IsArray(jChildren) && cJSON_GetArraySize(jChildren) > 0) {
            HMENU sub = CreatePopupMenu();
            buildMenuItems(sub, jChildren);
            AppendMenuW(menu, MF_POPUP | MF_STRING, (UINT_PTR)sub, wtitle);
        } else {
            UINT cmdId = gNextCmdId++;
            if (gMenuIdCount < MAX_MENU_ITEMS) {
                gMenuIdMap[gMenuIdCount].cmdId = cmdId;
                strncpy(gMenuIdMap[gMenuIdCount].id, itemId, 255);
                gMenuIdCount++;
            }
            UINT flags = MF_STRING;
            if (cJSON_IsFalse(cJSON_GetObjectItem(cfg, "enabled"))) flags |= MF_GRAYED;
            if (cJSON_IsTrue(cJSON_GetObjectItem(cfg, "checked"))) flags |= MF_CHECKED;
            AppendMenuW(menu, flags, cmdId, wtitle);
        }
        free(wtitle);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == gTaskbarCreatedMsg) { Shell_NotifyIconW(NIM_ADD, &gNid); return 0; }
    switch (msg) {
        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP) {
                emit("menuRequested", NULL);
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(gMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            }
            break;
        case WM_COMMAND: {
            for (UINT i=0; i<gMenuIdCount; i++) {
                if (gMenuIdMap[i].cmdId == LOWORD(wParam)) {
                    cJSON *p = cJSON_CreateObject();
                    cJSON_AddStringToObject(p, "id", gMenuIdMap[i].id);
                    emit("clicked", p);
                    break;
                }
            }
            break;
        }
        case WM_STDIN_CMD: {
            cJSON *m = (cJSON *)lParam;
            const char *meth = cJSON_GetStringValue(cJSON_GetObjectItem(m, "method"));
            cJSON *p = cJSON_GetObjectItem(m, "params");
            if (!strcmp(meth, "setMenu")) {
                if (gMenu) DestroyMenu(gMenu);
                gMenu = CreatePopupMenu(); gMenuIdCount = 0; gNextCmdId = 1;
                buildMenuItems(gMenu, cJSON_GetObjectItem(p, "items"));
            } else if (!strcmp(meth, "setIcon")) {
                size_t len; unsigned char *d = base64Decode(cJSON_GetStringValue(cJSON_GetObjectItem(p, "base64")), &len);
                if (d) {
                    WCHAR tmpPath[MAX_PATH], tmpFile[MAX_PATH];
                    GetTempPathW(MAX_PATH, tmpPath);
                    GetTempFileNameW(tmpPath, L"ico", 0, tmpFile);
                    HANDLE hf = CreateFileW(tmpFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hf != INVALID_HANDLE_VALUE) {
                        DWORD w; WriteFile(hf, d, (DWORD)len, &w, NULL); CloseHandle(hf);
                        HICON n = (HICON)LoadImageW(NULL, tmpFile, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
                        if (n) { if (gIcon) DestroyIcon(gIcon); gIcon = n; gNid.hIcon = gIcon; Shell_NotifyIconW(NIM_MODIFY, &gNid); }
                        DeleteFileW(tmpFile);
                    }
                    free(d);
                }
            } else if (!strcmp(meth, "setTooltip")) {
                MultiByteToWideChar(CP_UTF8, 0, cJSON_GetStringValue(cJSON_GetObjectItem(p, "text")), -1, gNid.szTip, MAX_TOOLTIP);
                Shell_NotifyIconW(NIM_MODIFY, &gNid);
            }
            cJSON_Delete(m); break;
        }
        case WM_DESTROY: 
            Shell_NotifyIconW(NIM_DELETE, &gNid); 
            if (gIcon) DestroyIcon(gIcon);
            PostQuitMessage(0); 
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static unsigned __stdcall stdinReaderThread(void *arg) {
    char chunk[4096], *buf = NULL; size_t bufLen = 0, bufCap = 0; DWORD read;
    while (ReadFile(gStdinHandle, chunk, sizeof(chunk), &read, NULL) && read > 0) {
        if (bufLen + read >= bufCap) { bufCap = (bufLen+read)*2+1; buf = realloc(buf, bufCap); }
        memcpy(buf + bufLen, chunk, read); bufLen += read;
        char *s = buf, *nl;
        while ((nl = memchr(s, '\n', bufLen - (s - buf)))) {
            *nl = '\0';
            cJSON *m = cJSON_Parse(s);
            if (m) PostMessage(gHwnd, WM_STDIN_CMD, 0, (LPARAM)m);
            s = nl + 1;
        }
        size_t rem = bufLen - (s - buf); if (rem > 0) memmove(buf, s, rem); bufLen = rem;
    }
    PostMessage(gHwnd, WM_CLOSE, 0, 0); return 0;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR lp, int n) {
    SetProcessDPIAware(); // Ensure sharp icons and text
    initB64(); InitializeCriticalSection(&gOutputLock);
    gStdinHandle = GetStdHandle(STD_INPUT_HANDLE); gStdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    
    WNDCLASSEXW wc = {sizeof(wc), 0, WndProc, 0, 0, hi, 0, 0, 0, 0, L"TrayJS", 0};
    RegisterClassExW(&wc);
    gHwnd = CreateWindowExW(0, L"TrayJS", L"TrayJS", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, hi, 0);
    
    gIcon = createDefaultIcon();
    gNid.cbSize = sizeof(gNid); gNid.hWnd = gHwnd; gNid.uID = 1; gNid.uFlags = NIF_ICON|NIF_MESSAGE|NIF_TIP;
    gNid.uCallbackMessage = WM_TRAYICON; gNid.hIcon = gIcon; wcscpy(gNid.szTip, L"Tray");
    Shell_NotifyIconW(NIM_ADD, &gNid);
    gMenu = CreatePopupMenu();
    
    gTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    _beginthreadex(NULL, 0, stdinReaderThread, NULL, 0, NULL);
    emit("ready", NULL);

    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}

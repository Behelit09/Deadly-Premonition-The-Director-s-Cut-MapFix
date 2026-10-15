#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Configuration globals
static volatile float g_MinZoom = 0.20f;
static volatile float g_DefaultZoom = 0.20f;
static int g_Enabled = 1;
static int g_EnableLog = 1;
static wchar_t g_LogPath[MAX_PATH] = { 0 };

static void LogMessage(const char* fmt, ...) {
    if (!g_EnableLog || g_LogPath[0] == L'\0') return;

    FILE* fp = _wfopen(g_LogPath, L"a");
    if (!fp) return;

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fclose(fp);
}

// -------------------------------------------------------------
// C Worker Functions called from assembly wrappers
// -------------------------------------------------------------

static void KeepAllTilesActive(void* pManager) {
    if (!pManager) return;
    int count = *(int*)((char*)pManager + 0x7C);
    char* pArray = *(char**)((char*)pManager + 0x80);
    if (pArray && count > 0 && count < 1000) {
        for (int i = 0; i < count; ++i) {
            char* tile = pArray + i * 0x50;
            tile[0x4C] = 1;
        }
    }
}

// Enforces min zoom limit (g_MinZoom) and max zoom limit (3.5f),
// and keeps all map tiles resident every frame.
void __cdecl ZoomClampWorker(void* pMap) {
    if (!pMap) return;
    float* pZoom = (float*)((char*)pMap + 0x8EE0);
    if (*pZoom < g_MinZoom) {
        *pZoom = g_MinZoom;
    } else if (*pZoom > 3.5f) {
        *pZoom = 3.5f;
    }

    KeepAllTilesActive((char*)pMap + 0x1638);
    KeepAllTilesActive((char*)pMap + 0x19D0);
}

// Sets initial zoom and preloads all 64 overworld tiles synchronously from DPSerial.003
void __cdecl ZoomInitWorker(void* pMap) {
    if (!pMap) return;
    float* pZoom = (float*)((char*)pMap + 0x8EE0);
    *pZoom = g_DefaultZoom;

    // DP.exe engine resource loader: int __cdecl LoadResource(int resId, int flags, int sync)
    typedef int (__cdecl *LoadResourceFn)(int resId, int flags, int sync);
    LoadResourceFn LoadResource = (LoadResourceFn)0x0040B4C0;

    // Overworld map tiles: IDs 0x0937 (SMAP00_00_000.XPC) through 0x0976 (SMAP00_00_063.XPC)
    // sync = 1 ensures tiles are completely resident before the first frame renders
    for (int i = 0; i < 64; ++i) {
        LoadResource(0x0937 + i, 0x18, 1);
    }
    LogMessage("[MapFix] Preloaded all 64 map tiles synchronously (IDs 0x0937..0x0976)\n");
}

// On-demand tile recovery during map render loop at 0x0065A32A
void* __cdecl FindOrLoadTileResourceWorker(void* pMgr, const char* name) {
    typedef void* (__thiscall *FindResFn)(void* mgr, const char* name, int zero);
    FindResFn OrigFind = (FindResFn)0x006B2C30;

    void* res = OrigFind(pMgr, name, 0);
    if (!res && name) {
        // Overworld tile format: "SMAP00_00_0%02d.XPC"
        if (strncmp(name, "SMAP00_00_0", 11) == 0) {
            int tileIndex = atoi(name + 11);
            if (tileIndex >= 0 && tileIndex < 64) {
                typedef int (__cdecl *LoadResourceFn)(int resId, int flags, int sync);
                LoadResourceFn LoadResource = (LoadResourceFn)0x0040B4C0;

                LoadResource(0x0937 + tileIndex, 0x18, 1);
                res = OrigFind(pMgr, name, 0);
                LogMessage("[MapFix] Recovered missing map tile on-demand: %s (ID=0x%04X, ptr=%p)\n",
                           name, 0x0937 + tileIndex, res);
            }
        }
    }
    return res;
}

// -------------------------------------------------------------
// Naked Assembly Wrappers
// -------------------------------------------------------------

__attribute__((naked)) void ZoomClampWrapper(void) {
    __asm__ volatile (
        ".intel_syntax noprefix\n"
        "pushad\n"
        "push esi\n"
        "call _ZoomClampWorker\n"
        "add esp, 4\n"
        "popad\n"
        "mov eax, 0x00700620\n"
        "call eax\n"
        "ret\n"
        ".att_syntax\n"
    );
}

__attribute__((naked)) void ZoomInitHook(void) {
    __asm__ volatile (
        ".intel_syntax noprefix\n"
        "fstp st(0)\n"
        "pushad\n"
        "push esi\n"
        "call _ZoomInitWorker\n"
        "add esp, 4\n"
        "popad\n"
        "ret\n"
        ".att_syntax\n"
    );
}

// Protects all 64 overworld map tiles (0x0937..0x0976) from being deleted by the resource manager
__attribute__((naked)) void DeleteResourceHook(void) {
    __asm__ volatile (
        ".intel_syntax noprefix\n"
        "mov eax, dword ptr [esp + 4]\n" // resId
        "cmp eax, 0x0937\n"
        "jl 1f\n"
        "cmp eax, 0x0976\n"
        "jg 1f\n"
        "xor eax, eax\n"
        "ret 4\n"                        // Bypass deletion: map tiles stay resident forever
        "1:\n"
        "push ebp\n"
        "mov ebp, esp\n"
        "sub esp, 0x0C\n"
        "mov eax, 0x006B3AA6\n"
        "jmp eax\n"
        ".att_syntax\n"
    );
}

// Protects all 64 overworld map tiles (0x0937..0x0976) from having refCount decremented/released
__attribute__((naked)) void ReleaseResourceHook(void) {
    __asm__ volatile (
        ".intel_syntax noprefix\n"
        "mov eax, dword ptr [esp + 4]\n" // resId
        "cmp eax, 0x0937\n"
        "jl 1f\n"
        "cmp eax, 0x0976\n"
        "jg 1f\n"
        "ret 12\n"                       // Bypass release: map tiles are never marked for unload (cleans 3 args / 12 bytes)
        "1:\n"
        "push ebp\n"
        "mov ebp, esp\n"
        "sub esp, 0x08\n"
        "mov eax, 0x006B2836\n"
        "jmp eax\n"
        ".att_syntax\n"
    );
}

// Hooks FindResourceByName call at 0x0065A32A inside map render loop
__attribute__((naked)) void FindOrLoadTileResourceHook(void) {
    __asm__ volatile (
        ".intel_syntax noprefix\n"
        "push dword ptr [esp + 4]\n"     // arg2: name
        "push ecx\n"                     // arg1: pMgr
        "call _FindOrLoadTileResourceWorker\n"
        "add esp, 8\n"
        "ret 8\n"                        // pops 2 caller args (push 0, push name) matching original __thiscall
        ".att_syntax\n"
    );
}

// -------------------------------------------------------------
// Patch Application
// -------------------------------------------------------------

static BOOL PatchMemory(void* addr, const void* bytes, size_t len) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogMessage("[MapFix] ERROR: VirtualProtect failed at %p (len=%zu), error=%u\n", addr, len, GetLastError());
        return FALSE;
    }

    memcpy(addr, bytes, len);
    VirtualProtect(addr, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    return TRUE;
}

static BOOL ApplyAllPatches(void) {
    // ---------------------------------------------------------
    // 1. Zoom-Out Unlock:
    // (a) Skip vanilla 1.0 floor clamp at 0x0065919F: 75 0D -> EB 0D
    // ---------------------------------------------------------
    static const unsigned char kZoomSkipExpected[2] = { 0x75, 0x0D };
    if (memcmp((void*)0x0065919F, kZoomSkipExpected, 2) != 0) {
        LogMessage("[MapFix] ERROR: Signature check failed at 0x0065919F (Zoom Skip)!\n");
        return FALSE;
    }
    static const unsigned char kZoomSkipPatch[2] = { 0xEB, 0x0D };
    if (!PatchMemory((void*)0x0065919F, kZoomSkipPatch, 2)) return FALSE;

    // (b) Hook 0x006591F0 call 0x00700620 -> call ZoomClampWrapper
    static const unsigned char kZoomCallExpected[5] = { 0xE8, 0x2B, 0x74, 0x0A, 0x00 };
    if (memcmp((void*)0x006591F0, kZoomCallExpected, 5) != 0) {
        LogMessage("[MapFix] ERROR: Signature check failed at 0x006591F0 (Zoom Call)!\n");
        return FALSE;
    }
    unsigned char zoomCallPatch[5];
    zoomCallPatch[0] = 0xE8;
    int32_t relZoomCall = (int32_t)((uintptr_t)ZoomClampWrapper - (0x006591F0 + 5));
    memcpy(&zoomCallPatch[1], &relZoomCall, 4);
    if (!PatchMemory((void*)0x006591F0, zoomCallPatch, 5)) return FALSE;
    LogMessage("[MapFix] Applied Map Zoom Limits: MinZoom=%.2f, MaxZoom=3.50\n", g_MinZoom);

    // (c) Initial Zoom & Preload Hook at 0x00661860
    if (g_DefaultZoom > 0.05f) {
        static const unsigned char kZoomInitExpected[6] = { 0xD9, 0x9E, 0xE0, 0x8E, 0x00, 0x00 };
        if (memcmp((void*)0x00661860, kZoomInitExpected, 6) == 0) {
            unsigned char zoomInitPatch[6];
            zoomInitPatch[0] = 0xE8; // call ZoomInitHook
            int32_t relInit = (int32_t)((uintptr_t)ZoomInitHook - (0x00661860 + 5));
            memcpy(&zoomInitPatch[1], &relInit, 4);
            zoomInitPatch[5] = 0x90; // NOP
            PatchMemory((void*)0x00661860, zoomInitPatch, 6);
            LogMessage("[MapFix] Applied Initial Zoom Hook & 64-Tile Preloader: %.2f\n", g_DefaultZoom);
        }
    }

    // ---------------------------------------------------------
    // 2. Map Panning Bounds Fix (Edge Clamping):
    //    0x00659534: replace fld [esp + 3Ch] with fldz (D9 EE 90 90)
    //    0x00659574: replace fld [esp + 38h] with fldz (D9 EE 90 90)
    //    Clamps Camera X and Camera Z directly to [0.0, 40000.0]
    // ---------------------------------------------------------
    static const unsigned char kPanClampXExpected[4] = { 0xD9, 0x44, 0x24, 0x3C };
    static const unsigned char kPanClampZExpected[4] = { 0xD9, 0x44, 0x24, 0x38 };
    if (memcmp((void*)0x00659534, kPanClampXExpected, 4) != 0 ||
        memcmp((void*)0x00659574, kPanClampZExpected, 4) != 0) {
        LogMessage("[MapFix] ERROR: Signature check failed at 0x00659534 / 0x00659574 (Pan Clamp)!\n");
        return FALSE;
    }
    static const unsigned char kFldzNop[4] = { 0xD9, 0xEE, 0x90, 0x90 };
    if (!PatchMemory((void*)0x00659534, kFldzNop, 4)) return FALSE;
    if (!PatchMemory((void*)0x00659574, kFldzNop, 4)) return FALSE;
    LogMessage("[MapFix] Applied Map Pan Clamp Fix: unlocked full 40000x40000 world extent\n");

    // ---------------------------------------------------------
    // 3. Map Tile Streaming Grid Expansion (Draw All 64 Tiles):
    //    0x0065A2ED: replace 20-byte 3x3 lookup with 'mov ebx, edx' + 18 NOPs
    //    0x0065A43C: replace 'cmp eax, 9' with 'cmp eax, 64' (83 F8 40)
    // ---------------------------------------------------------
    static const unsigned char kTileLookupExpected[20] = {
        0x0F, 0xB7, 0x84, 0x54, 0x68, 0x07, 0x00, 0x00,
        0x66, 0x85, 0xC0,
        0x0F, 0x8C, 0x39, 0x01, 0x00, 0x00,
        0x0F, 0xBF, 0xD8
    };
    static const unsigned char kTileLimitExpected[3] = { 0x83, 0xF8, 0x09 };

    if (memcmp((void*)0x0065A2ED, kTileLookupExpected, 20) != 0 ||
        memcmp((void*)0x0065A43C, kTileLimitExpected, 3) != 0) {
        LogMessage("[MapFix] ERROR: Signature check failed at 0x0065A2ED / 0x0065A43C (Tile Grid)!\n");
        return FALSE;
    }

    unsigned char tileLookupPatch[20];
    tileLookupPatch[0] = 0x89; tileLookupPatch[1] = 0xD3; // mov ebx, edx
    memset(&tileLookupPatch[2], 0x90, 18); // 18 NOPs

    static const unsigned char kTileLimitPatch[3] = { 0x83, 0xF8, 0x40 }; // cmp eax, 64

    if (!PatchMemory((void*)0x0065A2ED, tileLookupPatch, 20)) return FALSE;
    if (!PatchMemory((void*)0x0065A43C, kTileLimitPatch, 3)) return FALSE;
    LogMessage("[MapFix] Applied Map Tile Grid Expansion: rendering all 64 tiles (0..63)\n");

    // ---------------------------------------------------------
    // 4. Map Tile Keep-Active Patches (Prevent Culling on Pan):
    //    0x00659DFC: change 'mov byte ptr [eax + 4Ch], 0' to 'mov byte ptr [eax + 4Ch], 1'
    //    0x0065D32C: change 'mov byte ptr [ebx + 4Ch], 0' to 'mov byte ptr [ebx + 4Ch], 1'
    // ---------------------------------------------------------
    static const unsigned char kTileKeepActiveExpected[4] = { 0xC6, 0x40, 0x4C, 0x00 };
    static const unsigned char kTileKeepActivePatch[4]    = { 0xC6, 0x40, 0x4C, 0x01 };
    if (memcmp((void*)0x00659DFC, kTileKeepActiveExpected, 4) == 0) {
        PatchMemory((void*)0x00659DFC, kTileKeepActivePatch, 4);
    }

    static const unsigned char kTileKeepActive2Expected[4] = { 0xC6, 0x43, 0x4C, 0x00 };
    static const unsigned char kTileKeepActive2Patch[4]    = { 0xC6, 0x43, 0x4C, 0x01 };
    if (memcmp((void*)0x0065D32C, kTileKeepActive2Expected, 4) == 0) {
        PatchMemory((void*)0x0065D32C, kTileKeepActive2Patch, 4);
    }
    LogMessage("[MapFix] Applied Tile Keep-Active Patches (0x00659DFC, 0x0065D32C)\n");

    // ---------------------------------------------------------
    // 5. Tile Deletion & Release Immunity Hooks:
    //    Protects overworld map tiles (IDs 0x0937..0x0976) from EVER
    //    being deleted or marked for unload by the background streamer.
    // ---------------------------------------------------------
    static const unsigned char kDeleteExpected[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C };
    if (memcmp((void*)0x006B3AA0, kDeleteExpected, 6) == 0) {
        unsigned char delPatch[6];
        delPatch[0] = 0xE9; // jmp DeleteResourceHook
        int32_t relDel = (int32_t)((uintptr_t)DeleteResourceHook - (0x006B3AA0 + 5));
        memcpy(&delPatch[1], &relDel, 4);
        delPatch[5] = 0x90; // NOP
        if (PatchMemory((void*)0x006B3AA0, delPatch, 6)) {
            LogMessage("[MapFix] Applied DeleteResource Immunity Hook (0x006B3AA0)\n");
        }
    } else {
        LogMessage("[MapFix] WARNING: Signature mismatch at 0x006B3AA0 (DeleteResource)\n");
    }

    static const unsigned char kReleaseExpected[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08 };
    if (memcmp((void*)0x006B2830, kReleaseExpected, 6) == 0) {
        unsigned char relPatch[6];
        relPatch[0] = 0xE9; // jmp ReleaseResourceHook
        int32_t relRel = (int32_t)((uintptr_t)ReleaseResourceHook - (0x006B2830 + 5));
        memcpy(&relPatch[1], &relRel, 4);
        relPatch[5] = 0x90; // NOP
        if (PatchMemory((void*)0x006B2830, relPatch, 6)) {
            LogMessage("[MapFix] Applied ReleaseResource Immunity Hook (0x006B2830)\n");
        }
    } else {
        LogMessage("[MapFix] WARNING: Signature mismatch at 0x006B2830 (ReleaseResource)\n");
    }

    // ---------------------------------------------------------
    // 6. On-Demand Map Tile Loader Hook at 0x0065A32A:
    //    If FindResourceByName returns NULL during render, loads the tile
    //    synchronously and returns the freshly loaded resource.
    // ---------------------------------------------------------
    static const unsigned char kFindResExpected[5] = { 0xE8, 0x01, 0x89, 0x05, 0x00 };
    if (memcmp((void*)0x0065A32A, kFindResExpected, 5) == 0) {
        unsigned char findPatch[5];
        findPatch[0] = 0xE8; // call FindOrLoadTileResourceHook
        int32_t relFind = (int32_t)((uintptr_t)FindOrLoadTileResourceHook - (0x0065A32A + 5));
        memcpy(&findPatch[1], &relFind, 4);
        if (PatchMemory((void*)0x0065A32A, findPatch, 5)) {
            LogMessage("[MapFix] Applied On-Demand Tile Loader Hook (0x0065A32A)\n");
        }
    } else {
        LogMessage("[MapFix] WARNING: Signature mismatch at 0x0065A32A (FindResourceByName)\n");
    }

    return TRUE;
}

static void LoadConfig(HMODULE hModule) {
    wchar_t iniPath[MAX_PATH] = { 0 };
    wchar_t dirPath[MAX_PATH] = { 0 };

    GetModuleFileNameW(hModule, dirPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(dirPath, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }

    swprintf_s(iniPath, MAX_PATH, L"%s\\MapFix.ini", dirPath);
    swprintf_s(g_LogPath, MAX_PATH, L"%s\\MapFix.log", dirPath);

    FILE* fp = _wfopen(g_LogPath, L"w");
    if (fp) {
        fprintf(fp, "[MapFix] Deadly Premonition: Enhanced Map Mod\n");
        fprintf(fp, "[MapFix] Initializing...\n");
        fclose(fp);
    }

    g_Enabled = GetPrivateProfileIntW(L"MapFix", L"Enabled", 1, iniPath);

    wchar_t buf[64] = { 0 };
    GetPrivateProfileStringW(L"MapFix", L"MinZoom", L"0.20", buf, 64, iniPath);
    float minZ = (float)_wtof(buf);
    if (minZ < 0.05f || minZ > 3.0f) minZ = 0.20f;
    g_MinZoom = minZ;

    GetPrivateProfileStringW(L"MapFix", L"DefaultZoom", L"0.20", buf, 64, iniPath);
    float defZ = (float)_wtof(buf);
    if (defZ < 0.05f || defZ > 3.5f) defZ = 0.20f;
    g_DefaultZoom = defZ;

    g_EnableLog = GetPrivateProfileIntW(L"MapFix", L"EnableLog", 1, iniPath);

    LogMessage("[MapFix] Config: Enabled=%d, MinZoom=%.2f, DefaultZoom=%.2f\n",
               g_Enabled, g_MinZoom, g_DefaultZoom);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);

        wchar_t exePath[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
            return TRUE;
        }

        const wchar_t* exeName = wcsrchr(exePath, L'\\');
        exeName = (exeName != NULL) ? exeName + 1 : exePath;
        if (_wcsicmp(exeName, L"DP.exe") != 0) {
            return TRUE;
        }

        LoadConfig((HMODULE)hinstDLL);

        if (g_Enabled) {
            if (ApplyAllPatches()) {
                LogMessage("[MapFix] All patches applied successfully!\n");
            } else {
                LogMessage("[MapFix] One or more patches failed to apply.\n");
            }
        } else {
            LogMessage("[MapFix] Mod is disabled via MapFix.ini.\n");
        }
    }
    return TRUE;
}

#ifndef KOFVS_WIN_H
#define KOFVS_WIN_H

#include "util.h"

typedef unsigned long DWORD;
typedef long LONG;
typedef long HRESULT;
typedef void* HANDLE;
typedef void* HWND;
typedef void* HMODULE;

#define WINAPI __stdcall
#define INVALID_HANDLE ((HANDLE)(long long)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define CREATE_ALWAYS 2u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_APPEND_DATA 4u
#define PAGE_READWRITE 4u
#define PAGE_EXECUTE_READWRITE 0x40u

#if defined(_M_IX86) || defined(__i386__)
typedef u32 ksz;
#else
typedef u64 ksz;
#endif

typedef struct GUID {
    DWORD Data1;
    u16 Data2;
    u16 Data3;
    u8 Data4[8];
} GUID;

typedef struct SYSTEMTIME_ {
    u16 year, month, dow, day, hour, minute, second, ms;
} SYSTEMTIME_;

typedef struct RECT_ {
    LONG left, top, right, bottom;
} RECT_;

typedef struct POINT_ {
    LONG x, y;
} POINT_;

typedef struct MSG_ {
    HWND hwnd;
    u32 message;
    u64 wParam;
    i64 lParam;
    DWORD time;
    POINT_ pt;
    DWORD priv;
} MSG_;

typedef i64 (WINAPI *WNDPROC_)(HWND, u32, u64, i64);

typedef struct WNDCLASSEXW_ {
    u32 cbSize;
    u32 style;
    WNDPROC_ lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HMODULE hInstance;
    void* hIcon;
    void* hCursor;
    void* hbrBackground;
    const wch* lpszMenuName;
    const wch* lpszClassName;
    void* hIconSm;
} WNDCLASSEXW_;

__declspec(dllimport) HANDLE WINAPI CreateFileW(const wch* name, DWORD access, DWORD share, void* sec, DWORD disp, DWORD flags, HANDLE templ);
__declspec(dllimport) int WINAPI ReadFile(HANDLE file, void* buf, DWORD n, DWORD* read, void* ov);
__declspec(dllimport) int WINAPI WriteFile(HANDLE file, const void* buf, DWORD n, DWORD* written, void* ov);
__declspec(dllimport) int WINAPI CloseHandle(HANDLE handle);
__declspec(dllimport) int WINAPI FlushFileBuffers(HANDLE file);
__declspec(dllimport) DWORD WINAPI GetFileSize(HANDLE file, DWORD* high);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleW(const wch* name);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameW(HMODULE module, wch* buf, DWORD n);
__declspec(dllimport) void* WINAPI GetProcAddress(HMODULE module, const char* name);
__declspec(dllimport) HMODULE WINAPI LoadLibraryW(const wch* name);
__declspec(dllimport) int WINAPI VirtualProtect(void* addr, ksz size, DWORD neu, DWORD* old);
__declspec(dllimport) HANDLE WINAPI CreateThread(void* sec, ksz stack, DWORD (WINAPI *start)(void*), void* param, DWORD flags, DWORD* tid);
__declspec(dllimport) void WINAPI Sleep(DWORD ms);
__declspec(dllimport) u64 WINAPI GetTickCount64(void);
__declspec(dllimport) void WINAPI GetLocalTime(SYSTEMTIME_* time);
__declspec(dllimport) int WINAPI DisableThreadLibraryCalls(HMODULE module);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(DWORD which);
__declspec(dllimport) void WINAPI ExitProcess(u32 code);
__declspec(dllimport) DWORD WINAPI GetLastError(void);
__declspec(dllimport) u32 WINAPI GetSystemDirectoryW(wch* buf, u32 n);
__declspec(dllimport) DWORD WINAPI GetEnvironmentVariableW(const wch* name, wch* buf, DWORD n);
__declspec(dllimport) int WINAPI CreateDirectoryW(const wch* path, void* sec);

typedef struct MEMORY_BASIC_INFORMATION_ {
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    u16 PartitionId;
    u64 RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION_;

#define MEM_COMMIT_ 0x1000u
#define MEM_IMAGE_ 0x1000000u
#define PAGE_NOACCESS_ 0x01u
#define PAGE_GUARD_ 0x100u

__declspec(dllimport) ksz WINAPI VirtualQuery(const void* addr, MEMORY_BASIC_INFORMATION_* info, ksz len);

long _InterlockedExchange(long volatile* target, long value);
long _InterlockedCompareExchange(long volatile* target, long exchange, long comparand);
long _InterlockedIncrement(long volatile* target);
#if defined(__clang__)
#define _ReadWriteBarrier() __asm__ __volatile__("" ::: "memory")
#else
void _ReadWriteBarrier(void);
#pragma intrinsic(_InterlockedExchange, _InterlockedCompareExchange, _InterlockedIncrement, _ReadWriteBarrier)
#endif

#endif

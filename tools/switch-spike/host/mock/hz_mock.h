/* Host stand-in for the libnx pieces switch_mem.c and the spike use, built on
 * Linux memfd + mmap so aliasing and refusal behave like Horizon's. */
#pragma once
#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <malloc.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef u32 Result; typedef u32 Handle;
#define INVALID_HANDLE 0
#define CUR_PROCESS_HANDLE 0xFFFF8001
#define R_FAILED(r) ((r) != 0)
#define R_SUCCEEDED(r) ((r) == 0)
#define MAKERESULT(m,d) (((m)&0x1FF)|((d)<<9))
#define Module_Libnx 345
#define LibnxError_OutOfMemory 2
enum { Perm_None = 0, Perm_R = 1, Perm_W = 2, Perm_Rw = 3 };
enum { MemType_Unmapped = 0, MemState_Type = 0xFF };
enum { InfoType_TotalMemorySize = 6, InfoType_UsedMemorySize = 7 };
typedef struct { u64 addr, size; u32 type, attr, perm, ipc_refcount, device_refcount, padding; } MemoryInfo;
typedef struct VirtmemReservation VirtmemReservation;
void *mock_memalign(size_t a, size_t n);
void mock_free(void *p);
#define memalign mock_memalign
#define free mock_free
bool envIsSyscallHinted(unsigned svc);
Handle envGetOwnProcessHandle(void);
void virtmemLock(void); void virtmemUnlock(void);
void *virtmemFindAslr(size_t size, size_t guard);
void *virtmemFindCodeMemory(size_t size, size_t guard);
VirtmemReservation *virtmemAddReservation(void *mem, size_t size);
void virtmemRemoveReservation(VirtmemReservation *rv);
Result svcMapProcessCodeMemory(Handle p, u64 dst, u64 src, u64 size);
Result svcUnmapProcessCodeMemory(Handle p, u64 dst, u64 src, u64 size);
Result svcSetProcessMemoryPermission(Handle p, u64 addr, u64 size, u32 perm);
Result svcMapProcessMemory(void *dst, Handle p, u64 src, u64 size);
Result svcUnmapProcessMemory(void *dst, Handle p, u64 src, u64 size);
Result svcQueryMemory(MemoryInfo *mi, u32 *pi, u64 addr);
Result svcGetInfo(u64 *out, u32 id0, Handle h, u64 id1);
u64 armGetSystemTick(void); u64 armTicksToNs(u64 t);
void consoleInit(void *c); void consoleUpdate(void *c); void consoleExit(void *c);
typedef struct { int x; } PadState;
#define HidNpadStyleSet_NpadStandard 0
#define HidNpadButton_Plus 1
static inline void padConfigureInput(int a, int b) { (void)a; (void)b; }
static inline void padInitializeDefault(PadState *p) { (void)p; }
static inline void padUpdate(PadState *p) { (void)p; }
static inline u64 padGetButtonsDown(PadState *p) { (void)p; return 1; }
static inline bool appletMainLoop(void) { return true; }
static inline void svcSleepThread(u64 ns) { (void)ns; }

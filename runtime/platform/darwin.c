// Darwin / macOS / iOS host layer (runtime/platform.h).
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include "../platform.h"

#if defined(__APPLE__)
// MoltenVK references MTLResidencySetDescriptor (Metal 3.2 / iOS 18+).
// On iOS < 18 or when linking against older SDKs (e.g. iOS 17), provide a weak
// symbol so the Mach-O links cleanly without undefined symbol errors.
__attribute__((weak)) void *OBJC_CLASS_$_MTLResidencySetDescriptor = NULL;
#endif

// ---- guest address space
static uint64_t g_lo, g_hi;
#define COMMIT_CHUNK 0x10000ull
#define MAX_CHUNKS ((0x100000000ull + COMMIT_CHUNK - 1) / COMMIT_CHUNK)

enum { CHUNK_FREE = 0, CHUNK_BUSY, CHUNK_READY };
static _Atomic unsigned char g_chunk[MAX_CHUNKS];

static bool chunk_back(uint64_t base) {
    void *p = mmap((void *)(uintptr_t)base, COMMIT_CHUNK, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p == (void *)(uintptr_t)base) return true;
    return mprotect((void *)(uintptr_t)base, COMMIT_CHUNK, PROT_READ | PROT_WRITE) == 0;
}

static bool lazy_commit(uint64_t a) {
    if (a < g_lo || a >= g_hi) return false;
    uint64_t idx = (a - g_lo) / COMMIT_CHUNK;
    if (idx >= MAX_CHUNKS) return false;
    unsigned char expect = CHUNK_FREE;
    if (atomic_compare_exchange_strong(&g_chunk[idx], &expect, CHUNK_BUSY)) {
        bool ok = chunk_back(g_lo + idx * COMMIT_CHUNK);
        atomic_store(&g_chunk[idx], ok ? CHUNK_READY : CHUNK_FREE);
        return ok;
    }
    while (atomic_load(&g_chunk[idx]) == CHUNK_BUSY) atomic_thread_fence(memory_order_acquire);
    return atomic_load(&g_chunk[idx]) == CHUNK_READY;
}

static void fault_handler(int sig, siginfo_t *si, void *uc);

bool plat_reserve_guest(uint64_t base, uint64_t size) {
    fprintf(stderr, "[plat] plat_reserve_guest: requesting base=0x%llx, size=0x%llx (4 GB)...\n",
            (unsigned long long)base, (unsigned long long)size);
    void *p = mmap((void *)(uintptr_t)base, size, PROT_NONE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p != (void *)(uintptr_t)base) {
        fprintf(stderr, "[plat] FATAL: failed to reserve 0x%llx (size 0x%llx): mmap=%p (errno %d: %s)\n",
                (unsigned long long)base, (unsigned long long)size, p, errno, strerror(errno));
        FILE *ef = fopen("STARTUP_ERROR.txt", "w");
        if (ef) {
            fprintf(ef, "MHGURecomp Fatal Startup Error:\n");
            fprintf(ef, "Failed to reserve 4 GB virtual guest memory at 0x%llx.\n", (unsigned long long)base);
            fprintf(ef, "mmap error code %d: %s\n", errno, strerror(errno));
            fclose(ef);
        }
        return false;
    }
    fprintf(stderr, "[plat] Successfully reserved 4 GB guest memory at %p!\n", p);

    g_lo = base;
    g_hi = base + size;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    return true;
}

void plat_commit(uint64_t addr, uint64_t size) {
    if (!size) return;
    uint64_t a = addr & ~(COMMIT_CHUNK - 1), e = (addr + size + COMMIT_CHUNK - 1) & ~(COMMIT_CHUNK - 1);
    for (uint64_t c = a; c < e && c < g_hi; c += COMMIT_CHUNK) lazy_commit(c);
    if (e > g_hi)
        fprintf(stderr, "[plat] commit %llx+%llx past the guest window\n", (unsigned long long)addr,
                (unsigned long long)size);
}

int plat_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, int max, uint64_t *page) {
    (void)addr; (void)size; (void)pages; (void)max; (void)page;
    return -1;
}

bool plat_committed(uint64_t addr, uint64_t *end) {
    if (!g_lo || addr < g_lo || addr >= g_hi) { *end = addr + PLAT_PAGE; return false; }
    uint64_t idx = (addr - g_lo) / COMMIT_CHUNK;
    if (atomic_load(&g_chunk[idx]) != CHUNK_READY) {
        *end = g_lo + (idx + 1) * COMMIT_CHUNK;
        return false;
    }
    uint64_t n = idx;
    while (n + 1 < MAX_CHUNKS && n - idx < 4096 && atomic_load(&g_chunk[n + 1]) == CHUNK_READY) n++;
    *end = g_lo + (n + 1) * COMMIT_CHUNK;
    return true;
}

int plat_protect(uint64_t addr, uint64_t size, int prot) {
    int p = prot == PLAT_PROT_NONE ? PROT_NONE : prot == PLAT_PROT_READ ? PROT_READ : PROT_READ | PROT_WRITE;
    if (addr >= g_lo && addr < g_hi) plat_commit(addr, size);
    return mprotect((void *)(uintptr_t)addr, size, p) == 0 ? 0 : -1;
}

// ---- time
uint64_t plat_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void plat_sleep_until_ns(uint64_t t) {
    for (;;) {
        uint64_t now = plat_now_ns();
        if (now >= t) return;
        uint64_t left = t - now;
        if (left < 200000) { sched_yield(); continue; }
        struct timespec due = {(time_t)(left / 1000000000ull), (long)(left % 1000000000ull)};
        nanosleep(&due, NULL);
    }
}

// ---- process
void plat_init(void) {
    signal(SIGPIPE, SIG_IGN);
}

bool plat_exe_path(char *buf, size_t size) {
    uint32_t sz = (uint32_t)size;
    if (_NSGetExecutablePath(buf, &sz) == 0) {
        char real[PATH_MAX];
        if (realpath(buf, real)) {
            snprintf(buf, size, "%s", real);
        }
        return true;
    }
    return false;
}

static bool g_own_log;

static bool mkdir_p(const char *path) {
    char buf[PATH_MAX];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *s = buf + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(buf, 0777) != 0 && errno != EEXIST) return false;
        *s = '/';
    }
    return mkdir(buf, 0777) == 0 || errno == EEXIST;
}

static bool s_standalone_done = false;

void plat_standalone(void) {
    if (s_standalone_done) return;
    s_standalone_done = true;

    const char *home = getenv("HOME");
    char doc_dir[PATH_MAX] = "";
    if (home && *home) {
        snprintf(doc_dir, sizeof doc_dir, "%s/Documents", home);
        mkdir_p(doc_dir);
        if (chdir(doc_dir) != 0) { /* fallback to current dir */ }
    } else {
        char exe[PATH_MAX];
        if (plat_exe_path(exe, sizeof exe)) {
            char *s = strrchr(exe, '/');
            if (s) { *s = 0; if (chdir(exe) != 0) {} }
        }
    }

    mkdir_p("data/exefs");
    mkdir_p("data/romfs");
    FILE *rf = fopen("data/README.txt", "w");
    if (rf) {
        fprintf(rf, "Monster Hunter Generations Ultimate (Switch v1.4.0)\n\n"
                    "Place your extracted game files here:\n"
                    "  data/exefs/main\n"
                    "  data/exefs/sdk\n"
                    "  data/exefs/main.npdm\n"
                    "  data/romfs/... (extracted romfs files)\n");
        fclose(rf);
    }

    if (freopen("log.txt", "w", stderr)) {
        setvbuf(stderr, NULL, _IONBF, 0);
        dup2(fileno(stderr), STDOUT_FILENO);
        g_own_log = true;
    }
    fprintf(stderr, "[plat] Darwin early init complete. Working dir: %s\n", doc_dir[0] ? doc_dir : ".");
}

#if defined(__APPLE__)
// Run at dynamic load time before constructor(101) reserve_guest_space
__attribute__((constructor(100))) static void darwin_early_ctor(void) {
    plat_standalone();
}
#endif

bool plat_user_dir(char *buf, size_t size) {
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/Documents", home);
    mkdir_p(dir);
    snprintf(buf, size, "%s", dir);
    return true;
}

void plat_error_box(const char *msg) {
    fprintf(stderr, "\n*** %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MHGU Recomp", msg, NULL);
}

void plat_fatal_notice(void) {
    if (g_own_log) {
        plat_error_box("The game stopped because of an error. Check log.txt in Files > MHGURecomp.");
    }
}

// ---- diagnostics
const char *plat_symbol(uint64_t pc, uint64_t *offset) {
    static __thread char name[PATH_MAX], exe[PATH_MAX];
    Dl_info di;
    if (!dladdr((void *)(uintptr_t)pc, &di) || !di.dli_fname) return NULL;
    if (!exe[0] && !plat_exe_path(exe, sizeof exe)) snprintf(exe, sizeof exe, "-");
    if (!strcmp(di.dli_fname, exe)) return NULL;
    const char *slash = strrchr(di.dli_fname, '/');
    snprintf(name, sizeof name, "%s", slash ? slash + 1 : di.dli_fname);
    if (offset) *offset = pc - (uint64_t)(uintptr_t)di.dli_fbase;
    return name;
}

const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);

void plat_print_backtrace(void) {
    void *bt[64];
    int n = backtrace(bt, 64);
    Dl_info self;
    uint64_t exe = dladdr((void *)(uintptr_t)&plat_print_backtrace, &self) ? (uint64_t)(uintptr_t)self.dli_fbase : 0;
    for (int i = 0; i < n; i++) {
        uint64_t pc = (uint64_t)(uintptr_t)bt[i], off = 0;
        const char *s = rt_host_fn_name(pc, &off);
        if (!s) s = plat_symbol(pc, &off);
        if (s) fprintf(stderr, "  #%-2d %016llx %s+%llx\n", i, (unsigned long long)pc, s, (unsigned long long)off);
        else fprintf(stderr, "  #%-2d %016llx mhgurecomp+%llx\n", i, (unsigned long long)pc,
                     (unsigned long long)(exe ? pc - exe : 0));
    }
}

static const char *sig_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    default: return "signal";
    }
}

static void fault_handler(int sig, siginfo_t *si, void *uc) {
    ucontext_t *ctx = (ucontext_t *)uc;
    uint64_t fa = si && si->si_addr ? (uint64_t)(uintptr_t)si->si_addr : 0;
    if ((sig == SIGSEGV || sig == SIGBUS) && lazy_commit(fa)) return;

#if defined(__arm64__) || defined(__aarch64__)
    uint64_t pc = (uint64_t)ctx->uc_mcontext->__ss.__pc;
    uint64_t sp = (uint64_t)ctx->uc_mcontext->__ss.__sp;
    uint64_t lr = (uint64_t)ctx->uc_mcontext->__ss.__lr;
#elif defined(__x86_64__)
    uint64_t pc = (uint64_t)ctx->uc_mcontext->__ss.__rip;
    uint64_t sp = (uint64_t)ctx->uc_mcontext->__ss.__rsp;
    uint64_t lr = 0;
    uint64_t end = 0;
    if (plat_committed(sp, &end) && end - sp >= 8) lr = *(const uint64_t *)(uintptr_t)sp;
#else
    uint64_t pc = 0, sp = 0, lr = 0;
#endif

    char what[80];
    snprintf(what, sizeof what, "%s (code %d, addr %llx)", sig_name(sig), si ? si->si_code : 0,
             (unsigned long long)fa);
    rt_fault_report(what, fa, pc, lr);
    _exit(139);
}

void plat_crash_handler_install(void) {
    static char altstack[256 * 1024] __attribute__((aligned(16)));
    stack_t ss = {.ss_sp = altstack, .ss_size = sizeof altstack}, old;
    if (sigaltstack(NULL, &old) == 0 && !(old.ss_flags & SS_ONSTACK)) sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

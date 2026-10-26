// Network guard for game runs: injected with DYLD_INSERT_LIBRARIES, it keeps the
// game (and anything loaded into it) from reaching the network. The game's own
// network imports are connect, gethostbyname, sendto, socket and system (curl
// for UbiServices, Quazal over UDP, `system("open <url>")` for web pages); the
// interposed set below also covers the other common entry points.
//
//   allowed   AF_UNIX, loopback (127/8, ::1) and unspecified-destination sends
//             on already-connected sockets (their connect was checked)
//   refused   any other connect/connectx/sendto/sendmsg: -1, errno ENETUNREACH
//             name lookups: gethostbyname* -> NULL (h_errno HOST_NOT_FOUND),
//             getaddrinfo -> EAI_NONAME, except for literal loopback/localhost
//             system() and popen() of a command that opens a URL
//
// Every decision is logged. Environment:
//   RS_NETGUARD_LOG=<file>   log file (default: stderr)
//   RS_NETGUARD=log          log only, allow everything (to see what it tries)
//   RS_NETGUARD_BT=1         also log each refused call's backtrace; frames in
//                            the main executable are given as unslid addresses
#include <arpa/inet.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "../../../shims/include/interpose.h"

static FILE *g_log;
static int g_log_only;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct timespec g_t0;

static void logf_(const char *fmt, ...) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    long ms = (t.tv_sec - g_t0.tv_sec) * 1000 + (t.tv_nsec - g_t0.tv_nsec) / 1000000;
    pthread_mutex_lock(&g_lock);
    FILE *f = g_log ? g_log : stderr;
    fprintf(f, "%7ld ", ms);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
    pthread_mutex_unlock(&g_lock);
}

__attribute__((constructor)) static void netguard_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    const char *m = getenv("RS_NETGUARD");
    g_log_only = m && !strcmp(m, "log");
    const char *p = getenv("RS_NETGUARD_LOG");
    if (p && *p) g_log = fopen(p, "a");
    logf_("I netguard pid=%d mode=%s", getpid(), g_log_only ? "log-only" : "block");
}

// Returns 1 for destinations that stay on this machine.
static int is_local(const struct sockaddr *sa, socklen_t len, char *desc, size_t dsz) {
    if (!sa) { snprintf(desc, dsz, "(none)"); return 1; }
    switch (sa->sa_family) {
    case AF_UNIX:
        snprintf(desc, dsz, "unix:%s", len > offsetof(struct sockaddr_un, sun_path)
                                           ? ((const struct sockaddr_un *)sa)->sun_path : "");
        return 1;
    case AF_INET: {
        const struct sockaddr_in *in = (const void *)sa;
        char a[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &in->sin_addr, a, sizeof a);
        snprintf(desc, dsz, "%s:%u", a, ntohs(in->sin_port));
        return (ntohl(in->sin_addr.s_addr) >> 24) == 127;
    }
    case AF_INET6: {
        const struct sockaddr_in6 *in6 = (const void *)sa;
        char a[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &in6->sin6_addr, a, sizeof a);
        snprintf(desc, dsz, "[%s]:%u", a, ntohs(in6->sin6_port));
        if (IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr)) return 1;
        if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) return in6->sin6_addr.s6_addr[12] == 127;
        return 0;
    }
    default:
        snprintf(desc, dsz, "family %d", sa->sa_family);
        return 0;
    }
}

// Backtrace of the current call, main-image frames unslid (0x100000000-based).
static void log_bt(void) {
    static int on = -1;
    if (on < 0) on = getenv("RS_NETGUARD_BT") && *getenv("RS_NETGUARD_BT") == '1';
    if (!on) return;
    void *f[32];
    int n = backtrace(f, 32);
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    for (int i = 2; i < n; i++) {
        Dl_info di;
        const char *img = dladdr(f[i], &di) && di.dli_fname ? strrchr(di.dli_fname, '/') : NULL;
        if (di.dli_fbase == _dyld_get_image_header(0))
            logf_("    #%d main 0x%lx", i - 2, (unsigned long)((uintptr_t)f[i] - slide));
        else
            logf_("    #%d %s %s+0x%lx", i - 2, img ? img + 1 : "?", di.dli_sname ? di.dli_sname : "",
                  di.dli_saddr ? (unsigned long)((char *)f[i] - (char *)di.dli_saddr) : 0ul);
    }
}

static int check(const char *what, const struct sockaddr *sa, socklen_t len, void *caller) {
    char d[256];
    int local = is_local(sa, len, d, sizeof d);
    if (local) return 1;
    logf_("%s %s %s caller=%p", g_log_only ? "W" : "B", what, d, caller);
    log_bt();
    if (g_log_only) return 1;
    errno = ENETUNREACH;
    return 0;
}

static int rs_connect(int fd, const struct sockaddr *sa, socklen_t len) {
    if (!check("connect", sa, len, __builtin_return_address(0))) return -1;
    return connect(fd, sa, len);
}
RS_INTERPOSE(rs_connect, connect);

static int rs_connectx(int fd, const sa_endpoints_t *ep, sae_associd_t aid, unsigned int flags,
                       const struct iovec *iov, unsigned int iovcnt, size_t *len, sae_connid_t *cid) {
    if (ep && !check("connectx", ep->sae_dstaddr, ep->sae_dstaddrlen, __builtin_return_address(0))) return -1;
    return connectx(fd, ep, aid, flags, iov, iovcnt, len, cid);
}
RS_INTERPOSE(rs_connectx, connectx);

static ssize_t rs_sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to,
                         socklen_t tolen) {
    if (to && !check("sendto", to, tolen, __builtin_return_address(0))) return -1;
    return sendto(fd, buf, n, flags, to, tolen);
}
RS_INTERPOSE(rs_sendto, sendto);

static ssize_t rs_sendmsg(int fd, const struct msghdr *m, int flags) {
    if (m && m->msg_name && !check("sendmsg", m->msg_name, m->msg_namelen, __builtin_return_address(0))) return -1;
    return sendmsg(fd, m, flags);
}
RS_INTERPOSE(rs_sendmsg, sendmsg);

// --- name lookups ----------------------------------------------------------------
static int local_name(const char *name) {
    if (!name) return 1;  // getaddrinfo(NULL, port): a local bind address
    if (!strcmp(name, "localhost")) return 1;
    struct in_addr a4;
    struct in6_addr a6;
    if (inet_pton(AF_INET, name, &a4) == 1) return (ntohl(a4.s_addr) >> 24) == 127;
    if (inet_pton(AF_INET6, name, &a6) == 1) return IN6_IS_ADDR_LOOPBACK(&a6);
    return 0;
}

static int lookup_allowed(const char *what, const char *name, void *caller) {
    if (local_name(name)) return 1;
    logf_("%s %s \"%s\" caller=%p", g_log_only ? "W" : "B", what, name, caller);
    log_bt();
    return g_log_only;
}

static struct hostent *rs_gethostbyname(const char *name) {
    if (!lookup_allowed("gethostbyname", name, __builtin_return_address(0))) { h_errno = HOST_NOT_FOUND; return NULL; }
    return gethostbyname(name);
}
RS_INTERPOSE(rs_gethostbyname, gethostbyname);

static struct hostent *rs_gethostbyname2(const char *name, int af) {
    if (!lookup_allowed("gethostbyname2", name, __builtin_return_address(0))) { h_errno = HOST_NOT_FOUND; return NULL; }
    return gethostbyname2(name, af);
}
RS_INTERPOSE(rs_gethostbyname2, gethostbyname2);

static int rs_getaddrinfo(const char *node, const char *serv, const struct addrinfo *hints,
                          struct addrinfo **res) {
    if (!lookup_allowed("getaddrinfo", node, __builtin_return_address(0))) return EAI_NONAME;
    return getaddrinfo(node, serv, hints, res);
}
RS_INTERPOSE(rs_getaddrinfo, getaddrinfo);

// --- shell-outs that open web pages (system("open http://...")) -----------------
static int opens_url(const char *cmd) {
    return cmd && (strstr(cmd, "http://") || strstr(cmd, "https://") || strstr(cmd, "www."));
}

static int rs_system(const char *cmd) {
    if (opens_url(cmd)) {
        logf_("%s system \"%s\" caller=%p", g_log_only ? "W" : "B", cmd, __builtin_return_address(0));
        if (!g_log_only) return 127 << 8;  // as if the shell couldn't run it
    }
    return system(cmd);
}
RS_INTERPOSE(rs_system, system);

static FILE *rs_popen(const char *cmd, const char *mode) {
    if (opens_url(cmd)) {
        logf_("%s popen \"%s\" caller=%p", g_log_only ? "W" : "B", cmd, __builtin_return_address(0));
        if (!g_log_only) { errno = EACCES; return NULL; }
    }
    return popen(cmd, mode);
}
RS_INTERPOSE(rs_popen, popen);

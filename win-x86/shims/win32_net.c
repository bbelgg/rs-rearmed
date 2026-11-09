// ws2_32, iphlpapi and wininet: the game never reaches the network.
//
// The same rules as the Mac build's network guard (tools/ws8/netguard): sockets can be made and
// bound (the game opens UDP sockets for its own discovery), but nothing leaves the process: no
// host socket is ever created. connect and sendto to anything fail with WSAENETUNREACH, name
// lookups fail with WSAHOST_NOT_FOUND (literal loopback addresses resolve), receives would block,
// select finds nothing ready. The adapter list is empty and WinINet refuses everything. Every
// refused call is logged.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

#include "win32_shims.h"

#define WSAEWOULDBLOCK 10035
#define WSAENOTSOCK 10038
#define WSAEAFNOSUPPORT 10047
#define WSAENETUNREACH 10051
#define WSAENOTCONN 10057
#define WSAHOST_NOT_FOUND 11001
#define INVALID_SOCKET 0xffffffffu
#define SOCKET_ERROR 0xffffffffu

static __thread uint32_t wsa_err;
static void refused(const char *what) { fprintf(stderr, "w32: net: %s refused (offline)\n", what); }

// sockets: small integers the guest holds, with nothing behind them but a few flags
#define MAXS 256
#define SBASE 0x100u
static struct { int used, type, bound; uint16_t port; } socks[MAXS];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int sock_ok(uint32_t s) { return s >= SBASE && s - SBASE < MAXS && socks[s - SBASE].used; }

static void n_WSAStartup(rc_ctx *c) {  // WSADATA: version 2.2
  uint32_t d = w32_arg(c, 1);
  if (d) {
    memset(RC32_H(d), 0, 400);
    *(uint16_t *)RC32_H(d) = 0x0202;
    *(uint16_t *)RC32_H(d + 2) = 0x0202;
    snprintf((char *)RC32_H(d + 4), 257, "WinSock 2.0");
    snprintf((char *)RC32_H(d + 261), 129, "Running");
  }
  w32_ret(c, 0, 2);
}
static void n_WSACleanup(rc_ctx *c) { w32_ret(c, 0, 0); }
static void n_WSAGetLastError(rc_ctx *c) { w32_ret(c, wsa_err, 0); }
static void n_WSASetLastError(rc_ctx *c) { wsa_err = w32_arg(c, 0); w32_ret(c, 0, 1); }
static uint32_t new_sock(uint32_t af, uint32_t type) {
  if (af != 2 && af != 23) { wsa_err = WSAEAFNOSUPPORT; return INVALID_SOCKET; }  // AF_INET / AF_INET6
  pthread_mutex_lock(&mu);
  uint32_t s = INVALID_SOCKET;
  for (int k = 0; k < MAXS; k++)
    if (!socks[k].used) {
      socks[k].used = 1;
      socks[k].type = (int)type;
      socks[k].bound = 0;
      s = SBASE + (uint32_t)k;
      break;
    }
  pthread_mutex_unlock(&mu);
  if (w32_log) fprintf(stderr, "w32: net: socket(%u, %u) -> 0x%x (offline)\n", af, type, s);
  return s;
}
static void n_socket(rc_ctx *c) { w32_ret(c, new_sock(w32_arg(c, 0), w32_arg(c, 1)), 3); }
static void n_WSASocketA(rc_ctx *c) { w32_ret(c, new_sock(w32_arg(c, 0), w32_arg(c, 1)), 6); }
static void n_closesocket(rc_ctx *c) {
  uint32_t s = w32_arg(c, 0);
  int ok = sock_ok(s);
  if (ok) socks[s - SBASE].used = 0;
  else wsa_err = WSAENOTSOCK;
  w32_ret(c, ok ? 0 : SOCKET_ERROR, 1);
}
static void n_bind(rc_ctx *c) {
  uint32_t s = w32_arg(c, 0), a = w32_arg(c, 1);
  if (!sock_ok(s)) { wsa_err = WSAENOTSOCK; w32_ret(c, SOCKET_ERROR, 3); return; }
  socks[s - SBASE].bound = 1;
  socks[s - SBASE].port = a ? *(uint16_t *)RC32_H(a + 2) : 0;
  w32_ret(c, 0, 3);
}
static void n_connect(rc_ctx *c) {
  uint32_t a = w32_arg(c, 1);
  char what[64];
  const uint8_t *ip = a ? (const uint8_t *)RC32_H(a + 4) : 0;
  snprintf(what, sizeof what, "connect(%u.%u.%u.%u)", ip ? ip[0] : 0, ip ? ip[1] : 0, ip ? ip[2] : 0, ip ? ip[3] : 0);
  refused(what);
  wsa_err = WSAENETUNREACH;
  w32_ret(c, SOCKET_ERROR, 3);
}
static void n_send(rc_ctx *c) { refused("send"); wsa_err = WSAENOTCONN; w32_ret(c, SOCKET_ERROR, 4); }
static void n_sendto(rc_ctx *c) { refused("sendto"); wsa_err = WSAENETUNREACH; w32_ret(c, SOCKET_ERROR, 6); }
static void n_WSASendTo(rc_ctx *c) { refused("WSASendTo"); wsa_err = WSAENETUNREACH; w32_ret(c, SOCKET_ERROR, 9); }
static void n_recv(rc_ctx *c) { wsa_err = WSAEWOULDBLOCK; w32_ret(c, SOCKET_ERROR, 4); }
static void n_recvfrom(rc_ctx *c) { wsa_err = WSAEWOULDBLOCK; w32_ret(c, SOCKET_ERROR, 6); }
static void n_WSARecvFrom(rc_ctx *c) { wsa_err = WSAEWOULDBLOCK; w32_ret(c, SOCKET_ERROR, 9); }
static void n_WSAGetOverlappedResult(rc_ctx *c) { wsa_err = 996; w32_ret(c, 0, 5); }  // WSA_IO_INCOMPLETE
static void n_WSAIoctl(rc_ctx *c) { wsa_err = 10045; w32_ret(c, SOCKET_ERROR, 9); }  // WSAEOPNOTSUPP
static void n_ioctlsocket(rc_ctx *c) { w32_ret(c, sock_ok(w32_arg(c, 0)) ? 0 : SOCKET_ERROR, 3); }
static void n_setsockopt(rc_ctx *c) { w32_ret(c, sock_ok(w32_arg(c, 0)) ? 0 : SOCKET_ERROR, 5); }
static void n_getsockopt(rc_ctx *c) {
  uint32_t v = w32_arg(c, 3), l = w32_arg(c, 4);
  if (v && l && w32_rd32(l) >= 4) { w32_wr32(v, 0); w32_wr32(l, 4); }
  w32_ret(c, sock_ok(w32_arg(c, 0)) ? 0 : SOCKET_ERROR, 5);
}
static void n_shutdown(rc_ctx *c) { w32_ret(c, 0, 2); }
static void n_getsockname(rc_ctx *c) {  // 0.0.0.0:<the bound port>
  uint32_t s = w32_arg(c, 0), a = w32_arg(c, 1), l = w32_arg(c, 2);
  if (!sock_ok(s)) { wsa_err = WSAENOTSOCK; w32_ret(c, SOCKET_ERROR, 3); return; }
  memset(RC32_H(a), 0, 16);
  *(uint16_t *)RC32_H(a) = 2;
  *(uint16_t *)RC32_H(a + 2) = socks[s - SBASE].port;
  if (l) w32_wr32(l, 16);
  w32_ret(c, 0, 3);
}
static void n_getpeername(rc_ctx *c) { wsa_err = WSAENOTCONN; w32_ret(c, SOCKET_ERROR, 3); }
static void n_select(rc_ctx *c) {  // nothing is ever ready: clear the sets, wait out the timeout
  for (int k = 1; k <= 3; k++)
    if (w32_arg(c, k)) w32_wr32(w32_arg(c, k), 0);
  // no timeout is a wait for as long as it takes: the same 100 ms at most, not none (a loop on it would spin)
  uint32_t tv = w32_arg(c, 4);
  uint64_t us = tv ? (uint64_t)w32_rd32(tv) * 1000000 + w32_rd32(tv + 4) : 100000;
  if (us > 100000) us = 100000;
  if (us) {
    struct timespec t = {0, (long)us * 1000};
    nanosleep(&t, 0);
  }
  w32_ret(c, 0, 5);
}
static void n___WSAFDIsSet(rc_ctx *c) { w32_ret(c, 0, 2); }
static void n_WSACreateEvent(rc_ctx *c) {
  uint32_t a[4] = {0, 1, 0, 0};  // a manual-reset event, through kernel32's CreateEventA
  rc_fn f = w32_import("kernel32.dll!CreateEventA");
  uint32_t esp = (uint32_t)c->r[4];
  c->r[4] = esp - 16;
  memcpy(RC32_H(c->r[4]), a, 16);
  f(c);
  c->r[4] = esp;
}
static void n_WSAResetEvent(rc_ctx *c) { w32_import("kernel32.dll!ResetEvent")(c); }
static void n_WSACloseEvent(rc_ctx *c) { w32_import("kernel32.dll!CloseHandle")(c); }
// (count, events, all, ms, alertable): WaitForMultipleObjectsEx's arguments and results
static void n_WSAWaitForMultipleEvents(rc_ctx *c) { w32_import("kernel32.dll!WaitForMultipleObjectsEx")(c); }
static uint16_t bswap16(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }
static void n_htons(rc_ctx *c) { w32_ret(c, bswap16((uint16_t)w32_arg(c, 0)), 1); }
static void n_ntohs(rc_ctx *c) { w32_ret(c, bswap16((uint16_t)w32_arg(c, 0)), 1); }
static void n_htonl(rc_ctx *c) { w32_ret(c, __builtin_bswap32(w32_arg(c, 0)), 1); }
static void n_ntohl(rc_ctx *c) { w32_ret(c, __builtin_bswap32(w32_arg(c, 0)), 1); }
static int dotted(const char *s, uint8_t ip[4]) {
  unsigned a, b, cc, d;
  char t;
  if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &cc, &d, &t) != 4 || a > 255 || b > 255 || cc > 255 || d > 255) return 0;
  ip[0] = (uint8_t)a; ip[1] = (uint8_t)b; ip[2] = (uint8_t)cc; ip[3] = (uint8_t)d;
  return 1;
}
static void n_inet_addr(rc_ctx *c) {
  uint8_t ip[4];
  uint32_t v = 0xffffffffu;  // INADDR_NONE
  if (dotted(w32_cstr(w32_arg(c, 0)), ip)) memcpy(&v, ip, 4);
  w32_ret(c, v, 1);
}
static void n_inet_ntoa(rc_ctx *c) {
  static __thread uint32_t buf;
  if (!buf) buf = w32_galloc(16);
  uint32_t a = w32_arg(c, 0);
  const uint8_t *ip = (const uint8_t *)&a;
  snprintf((char *)RC32_H(buf), 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  w32_ret(c, buf, 1);
}
static void n_gethostname(rc_ctx *c) {
  uint32_t b = w32_arg(c, 0), n = w32_arg(c, 1);
  if (n < 3) { wsa_err = 10014; w32_ret(c, SOCKET_ERROR, 2); return; }  // WSAEFAULT
  memcpy(RC32_H(b), "PC", 3);
  w32_ret(c, 0, 2);
}
static int loopback_name(const char *n) { return !strcasecmp(n, "localhost") || !strcmp(n, "127.0.0.1"); }
static void n_gethostbyname(rc_ctx *c) {
  const char *n = w32_cstr(w32_arg(c, 0));
  if (n && (loopback_name(n) || !strcmp(n, "PC"))) {  // HOSTENT for 127.0.0.1
    static uint32_t he;
    if (!he) {
      he = w32_galloc(64);
      uint32_t addr = he + 32, list = he + 40, name = w32_gstr("localhost");
      w32_wr32(he, name);
      w32_wr32(he + 4, he + 48);           // no aliases
      *(uint16_t *)RC32_H(he + 8) = 2;     // AF_INET
      *(uint16_t *)RC32_H(he + 10) = 4;
      w32_wr32(he + 12, list);
      memcpy(RC32_H(addr), "\x7f\0\0\1", 4);
      w32_wr32(list, addr);
    }
    w32_ret(c, he, 1);
    return;
  }
  char what[300];
  snprintf(what, sizeof what, "gethostbyname(\"%s\")", n ? n : "");
  refused(what);
  wsa_err = WSAHOST_NOT_FOUND;
  w32_ret(c, 0, 1);
}
static void n_getaddrinfo(rc_ctx *c) {
  const char *n = w32_cstr(w32_arg(c, 0));
  char what[300];
  snprintf(what, sizeof what, "getaddrinfo(\"%s\")", n ? n : "");
  refused(what);
  if (w32_arg(c, 3)) w32_wr32(w32_arg(c, 3), 0);
  w32_ret(c, WSAHOST_NOT_FOUND, 4);
}
static void n_FreeAddrInfoW(rc_ctx *c) { w32_ret(c, 0, 1); }
static void n_freeaddrinfo(rc_ctx *c) { w32_ret(c, 0, 1); }

#define E(n) {#n, n_##n}
static const w32_export ws2[] = {
    E(WSAStartup), E(WSACleanup), E(WSAGetLastError), E(WSASetLastError), E(socket), E(WSASocketA),
    E(closesocket), E(bind), E(connect), E(send), E(sendto), E(WSASendTo), E(recv), E(recvfrom), E(WSARecvFrom),
    E(WSAGetOverlappedResult), E(WSAIoctl), E(ioctlsocket), E(setsockopt), E(getsockopt), E(shutdown),
    E(getsockname), E(getpeername), E(select), E(__WSAFDIsSet), E(WSACreateEvent), E(WSAResetEvent),
    E(WSACloseEvent), E(WSAWaitForMultipleEvents), E(htons), E(ntohs), E(htonl), E(ntohl), E(inet_addr), E(inet_ntoa), E(gethostname), E(gethostbyname), E(getaddrinfo),
    E(FreeAddrInfoW), E(freeaddrinfo),
};
W32_DLL(w32_dll_ws2_32, "ws2_32.dll", ws2, 0);

// iphlpapi: no adapters
static void n_GetAdaptersAddresses(rc_ctx *c) {
  if (w32_arg(c, 4)) w32_wr32(w32_arg(c, 4), 0);
  w32_ret(c, 232, 5);  // ERROR_NO_DATA
}
static void n_GetBestInterface(rc_ctx *c) { w32_ret(c, 1231, 2); }  // ERROR_NETWORK_UNREACHABLE
static void n_GetIfEntry(rc_ctx *c) { w32_ret(c, 1231, 1); }
static const w32_export iph[] = {E(GetAdaptersAddresses), E(GetBestInterface), E(GetIfEntry)};
W32_DLL(w32_dll_iphlpapi, "iphlpapi.dll", iph, 0);

// wininet
static void n_InternetSetCookieExA(rc_ctx *c) {
  refused("InternetSetCookieEx");
  w32_set_error(c, 12007);  // ERROR_INTERNET_NAME_NOT_RESOLVED
  w32_ret(c, 0, 5);
}
static void n_InternetOpenA(rc_ctx *c) { refused("InternetOpen"); w32_set_error(c, 12007); w32_ret(c, 0, 5); }
static void n_InternetGetConnectedState(rc_ctx *c) { if (w32_arg(c, 0)) w32_wr32(w32_arg(c, 0), 0x20); w32_ret(c, 0, 2); }  // offline
static const w32_export inet[] = {E(InternetSetCookieExA), E(InternetOpenA), E(InternetGetConnectedState)};
W32_DLL(w32_dll_wininet, "wininet.dll", inet, 0);

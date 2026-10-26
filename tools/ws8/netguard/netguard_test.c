// Self-test for libnetguard: run with the guard injected. Uses only targets
// that can't reach anyone even if the guard failed to load: TEST-NET-1
// (192.0.2.0/24, RFC 5737), the .invalid TLD, and a "URL" command that is `true`.
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } \
                           else { printf("ok    " __VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    struct sockaddr_in far = {.sin_len = sizeof far, .sin_family = AF_INET, .sin_port = htons(443)};
    inet_pton(AF_INET, "192.0.2.1", &far.sin_addr);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    errno = 0;
    int r = connect(s, (struct sockaddr *)&far, sizeof far);
    CHECK(r == -1 && errno == ENETUNREACH, "connect to 192.0.2.1:443 refused (r=%d errno=%d)", r, errno);
    close(s);

    int u = socket(AF_INET, SOCK_DGRAM, 0);
    errno = 0;
    ssize_t n = sendto(u, "x", 1, 0, (struct sockaddr *)&far, sizeof far);
    CHECK(n == -1 && errno == ENETUNREACH, "UDP sendto 192.0.2.1 refused (n=%zd errno=%d)", n, errno);
    close(u);

    // loopback still works: listen, connect, accept
    int l = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in lo = {.sin_len = sizeof lo, .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t ll = sizeof lo;
    bind(l, (struct sockaddr *)&lo, sizeof lo);
    listen(l, 1);
    getsockname(l, (struct sockaddr *)&lo, &ll);
    int c = socket(AF_INET, SOCK_STREAM, 0);
    r = connect(c, (struct sockaddr *)&lo, sizeof lo);
    CHECK(r == 0, "connect to loopback allowed (r=%d errno=%d)", r, errno);
    close(c);
    close(l);

    CHECK(gethostbyname("netguard-test.invalid") == NULL, "gethostbyname refused");
    struct addrinfo *ai = NULL;
    r = getaddrinfo("netguard-test.invalid", "443", NULL, &ai);
    CHECK(r == EAI_NONAME, "getaddrinfo refused (r=%d)", r);
    r = getaddrinfo("localhost", "80", NULL, &ai);
    CHECK(r == 0, "getaddrinfo localhost allowed (r=%d)", r);
    if (!r) freeaddrinfo(ai);

    r = system("true https://netguard-test.invalid/");
    CHECK(r == (127 << 8), "system() with a URL blocked (r=%d)", r);
    r = system("true");
    CHECK(r == 0, "plain system() allowed (r=%d)", r);

    printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures != 0;
}

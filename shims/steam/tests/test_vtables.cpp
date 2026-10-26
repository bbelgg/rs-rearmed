// Checks mock-interface vtable slots against the indices extracted from the
// game's original libsteam_api.dylib flat-API thunks.
#include <stdio.h>

int shim_check_vtables(void (*report)(const char *, long, long));

static void report(const char *what, long got, long want) {
    fprintf(stderr, "FAIL %s: slot %ld, expected %ld\n", what, got, want);
}

int main() {
    int bad = shim_check_vtables(report);
    printf("vtables: %s\n", bad ? "FAILED" : "all slots match");
    return bad != 0;
}

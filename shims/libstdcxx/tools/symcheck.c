// Usage: symcheck <dylib> < symbols.txt
// Reads Mach-O symbol names (leading '_') one per line, dlopens <dylib> and
// prints every name dlsym can't resolve. Exit status is the number missing
// (capped at 255). Build it fat and run it under `arch -x86_64` to check the
// Rosetta side of the same library.
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <dylib> < symbols.txt\n", argv[0]);
        return 64;
    }
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 255;
    }
    char line[4096];
    int total = 0, missing = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] != '_')
            continue;
        total++;
        if (!dlsym(h, line + 1)) {  // dlsym takes the C name, without '_'
            printf("%s\n", line);
            missing++;
        }
    }
    fprintf(stderr, "%d/%d symbols resolved\n", total - missing, total);
    return missing > 255 ? 255 : missing;
}

// Compiled against libc++ (the normal toolchain C++ library). Catches
// exceptions thrown from libstdc++ code to show both libraries share
// libc++abi's runtime and typeinfo.
#include <new>
#include <stdexcept>

int libcxx_catch(void (*thrower)()) {
    try {
        thrower();
    } catch (const std::length_error &) {
        return 1;
    } catch (const std::bad_alloc &) {
        return 2;
    } catch (...) {
        return 3;
    }
    return 0;
}

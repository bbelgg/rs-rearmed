// A library built without rc_mod.h, as a mod of 1.4.x is: the Mac loads it, Learn & Play doesn't run any of it.
int plain_mod_ran __attribute__((visibility("default")));
__attribute__((constructor)) static void init(void) { plain_mod_ran = 1; }

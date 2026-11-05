// Keys for a running game from a FIFO, for test runs of either edition: load it with DYLD_INSERT_LIBRARIES,
// and `echo "key return" > FIFO` presses Return in the game's window.
// A script with waits (LP_INPUT_SCRIPT, Learn & Play's) is the way to repeat a known route; this is the way to
// find one, a key at a time, looking at the snapshots (RS_GL_SNAPSHOT_DIR) in between.
//
//   tools/ws8/keyfifo/build.sh OUTDIR
//   RS_KEY_FIFO=/path/fifo DYLD_INSERT_LIBRARIES=OUTDIR/libkeyfifo.dylib <the game's executable>
//
// One command a line:
//   key NAME [MS]      press and hold for MS ms (default 120: a down and up in one frame is lost for text).
//                      NAME: return esc delete fwddelete tab space up down left right shift ctrl alt cmd, one
//                      letter or digit, or a macOS key code in hex (0x24)
//   text STRING [MS]   each character of STRING, MS ms between them (default 1500: the song search drops faster ones)
//   click X Y          a left click at X, Y in the window's content, in points from the top left (into the event
//                      queue, as keys; to the window with RS_KEY_DIRECT=1)
//   quit               the app menu's Command-Q item, as a person chooses it
//   front              the game's app and window in front (the engine takes keys only while it has the focus)
// Lines are logged to stderr as "keyfifo: ...". Keys go into the game's own event queue, so nothing else on the
// Mac gets them; the game's window has to be its key window. RS_KEY_DIRECT=1 (the Mac edition) sends them straight
// to the window instead, in front or not.
#import <Cocoa/Cocoa.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const struct { const char *name; int code; NSEventModifierFlags mod; } keys[] = {
    {"return", 0x24, 0},  {"esc", 0x35, 0},     {"delete", 0x33, 0}, {"fwddelete", 0x75, 0},
    {"tab", 0x30, 0},     {"space", 0x31, 0},   {"up", 0x7e, 0},     {"down", 0x7d, 0},
    {"left", 0x7b, 0},    {"right", 0x7c, 0},   {"/", 0x2c, 0},      {"-", 0x1b, 0},
    {"shift", 0x38, NSEventModifierFlagShift},  {"ctrl", 0x3b, NSEventModifierFlagControl},
    {"lpshift", 0x38, 0},                       {"lpctrl", 0x3b, 0},  // Learn & Play's input takes these as plain keys
    {"alt", 0x3a, NSEventModifierFlagOption},   {"cmd", 0x37, NSEventModifierFlagCommand},
    {"a", 0x00, 0}, {"b", 0x0b, 0}, {"c", 0x08, 0}, {"d", 0x02, 0}, {"e", 0x0e, 0}, {"f", 0x03, 0},
    {"g", 0x05, 0}, {"h", 0x04, 0}, {"i", 0x22, 0}, {"j", 0x26, 0}, {"k", 0x28, 0}, {"l", 0x25, 0},
    {"m", 0x2e, 0}, {"n", 0x2d, 0}, {"o", 0x1f, 0}, {"p", 0x23, 0}, {"q", 0x0c, 0}, {"r", 0x0f, 0},
    {"s", 0x01, 0}, {"t", 0x11, 0}, {"u", 0x20, 0}, {"v", 0x09, 0}, {"w", 0x0d, 0}, {"x", 0x07, 0},
    {"y", 0x10, 0}, {"z", 0x06, 0}, {"0", 0x1d, 0}, {"1", 0x12, 0}, {"2", 0x13, 0}, {"3", 0x14, 0},
    {"4", 0x15, 0}, {"5", 0x17, 0}, {"6", 0x16, 0}, {"7", 0x1a, 0}, {"8", 0x1c, 0}, {"9", 0x19, 0},
};

static NSWindow *game_window(void) {
  for (NSWindow *w in NSApp.windows)
    if (w.isVisible) return w;
  return nil;
}

static void post_key(NSEventType type, int code, NSEventModifierFlags flags, const char *chars) {
  dispatch_sync(dispatch_get_main_queue(), ^{
    NSWindow *w = game_window();
    if (!w) { fprintf(stderr, "keyfifo: no window\n"); return; }
    // the Mac shell reads the first character of every key event
    unichar u = ' ';
    switch (code) {
      case 0x24: u = '\r'; break;
      case 0x35: u = 0x1b; break;
      case 0x33: u = 0x7f; break;
      case 0x30: u = '\t'; break;
      case 0x75: u = NSDeleteFunctionKey; break;
      case 0x7e: u = NSUpArrowFunctionKey; break;
      case 0x7d: u = NSDownArrowFunctionKey; break;
      case 0x7b: u = NSLeftArrowFunctionKey; break;
      case 0x7c: u = NSRightArrowFunctionKey; break;
    }
    NSString *s = chars && *chars ? [NSString stringWithUTF8String:chars] : [NSString stringWithCharacters:&u length:1];
    NSEvent *e = [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags
                                 timestamp:[NSProcessInfo processInfo].systemUptime windowNumber:w.windowNumber
                                   context:nil characters:s charactersIgnoringModifiers:s isARepeat:NO keyCode:code];
    // the queue reaches Learn & Play's local monitor, but only the key window; the Mac shell takes the event directly
    if (getenv("RS_KEY_DIRECT")) [w sendEvent:e];
    else [NSApp postEvent:e atStart:NO];
  });
}

// Modifier keys arrive as flagsChanged.
static void send_flags(int code, NSEventModifierFlags flags) {
  dispatch_sync(dispatch_get_main_queue(), ^{
    NSWindow *w = game_window();
    if (!w) return;
    NSEvent *e = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint modifierFlags:flags
                                 timestamp:[NSProcessInfo processInfo].systemUptime windowNumber:w.windowNumber
                                   context:nil characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:code];
    [NSApp postEvent:e atStart:NO];
  });
}

static int find_key(const char *name, NSEventModifierFlags *mod, const char **chars) {
  *mod = 0;
  *chars = strlen(name) == 1 ? name : "";
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
    if (!strcmp(keys[i].name, name)) { *mod = keys[i].mod; return keys[i].code; }
  if (!strncmp(name, "0x", 2)) return (int)strtol(name, NULL, 16);
  return -1;
}

static void press(const char *name, int ms) {
  NSEventModifierFlags mod;
  const char *chars;
  int code = find_key(name, &mod, &chars);
  if (code < 0) { fprintf(stderr, "keyfifo: no key \"%s\"\n", name); return; }
  fprintf(stderr, "keyfifo: key %s\n", name);
  if (mod) {
    send_flags(code, mod);
    usleep((useconds_t)ms * 1000);
    send_flags(code, 0);
  } else {
    post_key(NSEventTypeKeyDown, code, 0, chars);
    usleep((useconds_t)ms * 1000);
    post_key(NSEventTypeKeyUp, code, 0, chars);
  }
}

static void click(double x, double y) {
  fprintf(stderr, "keyfifo: click %.0f %.0f\n", x, y);
  for (int i = 0; i < 2; i++) {
    dispatch_sync(dispatch_get_main_queue(), ^{
      NSWindow *w = game_window();
      if (!w) return;
      NSPoint p = NSMakePoint(x, w.contentView.bounds.size.height - y);
      NSEvent *e = [NSEvent mouseEventWithType:i ? NSEventTypeLeftMouseUp : NSEventTypeLeftMouseDown location:p
                                 modifierFlags:0 timestamp:[NSProcessInfo processInfo].systemUptime
                                  windowNumber:w.windowNumber context:nil eventNumber:0 clickCount:1
                                      pressure:i ? 0 : 1];
      if (getenv("RS_KEY_DIRECT")) [w sendEvent:e];
      else [NSApp postEvent:e atStart:NO];  // Learn & Play reads its own queue
    });
    usleep(120000);
  }
}

static void menu_quit(void) {
  fprintf(stderr, "keyfifo: quit\n");
  dispatch_async(dispatch_get_main_queue(), ^{
    for (NSMenuItem *top in NSApp.mainMenu.itemArray)
      for (NSInteger i = 0; i < top.submenu.numberOfItems; i++)
        if ([[top.submenu itemAtIndex:i].keyEquivalent isEqualToString:@"q"]) { [top.submenu performActionForItemAtIndex:i]; return; }
    fprintf(stderr, "keyfifo: no Command-Q item\n");
  });
}

static void front(void) {
  fprintf(stderr, "keyfifo: front\n");
  dispatch_sync(dispatch_get_main_queue(), ^{
    [NSApp activateIgnoringOtherApps:YES];
    [game_window() makeKeyAndOrderFront:nil];
  });
}

static void run_line(char *line) {
  char name[256];
  int ms = 0, n;
  double x, y;
  line[strcspn(line, "\r\n")] = 0;
  if ((n = sscanf(line, "key %255s %d", name, &ms)) >= 1) press(name, n == 2 ? ms : 120);
  else if ((n = sscanf(line, "text %255s %d", name, &ms)) >= 1) {
    for (char *c = name; *c; c++) {
      char one[2] = {*c, 0};
      press(one, 120);
      usleep((useconds_t)(n == 2 ? ms : 1500) * 1000);
    }
  } else if (sscanf(line, "click %lf %lf", &x, &y) == 2) click(x, y);
  else if (!strcmp(line, "quit")) menu_quit();
  else if (!strcmp(line, "front")) front();
  else if (*line) fprintf(stderr, "keyfifo: what is \"%s\"?\n", line);
}

static void *reader(void *arg) {
  // read and write, so that a writer's close is no end of file. Plain read(): a stdio stream blocked here holds its
  // lock, and the game's exit flushes every stream.
  int fd = open((const char *)arg, O_RDWR);
  char line[512];
  size_t n = 0;
  for (char c; fd >= 0 && read(fd, &c, 1) == 1;) {
    if (c == '\n' || n == sizeof line - 1) {
      line[n] = 0;
      n = 0;
      @autoreleasepool { run_line(line); }
    } else {
      line[n++] = c;
    }
  }
  return NULL;
}

__attribute__((constructor)) static void keyfifo_start(void) {
  const char *path = getenv("RS_KEY_FIFO");
  if (!path || !*path) return;
  mkfifo(path, 0600);
  pthread_t t;
  pthread_create(&t, NULL, reader, strdup(path));
  pthread_detach(t);
}

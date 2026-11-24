// Author: AJ Tomalino
// File: server.c
// Purpose: Maintain a controller state for the target console to communicate
// with, get updates from input_reader.c Compilation: gcc server.c

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define SERVER "server.sock"
#define GADGET "/sys/kernel/config/usb_gadget/inputgw"
#define TICK_NS 4000000L /* 250 Hz */

/* ------------------------------------------------------------ gadget ---- */

/* HORI Pokken pad: 16 buttons, hat, 4 axes, 1 vendor byte (8-byte report). */
static const uint8_t report_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x15, 0x00, 0x25, 0x01, 0x35,
    0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x10, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x10, 0x81, 0x02, 0x05, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01,
    0x75, 0x04, 0x95, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42, 0x65,
    0x00, 0x95, 0x01, 0x81, 0x01, 0x26, 0xFF, 0x00, 0x46, 0xFF, 0x00,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x75, 0x08, 0x95,
    0x04, 0x81, 0x02, 0x06, 0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81,
    0x02, 0x0A, 0x21, 0x26, 0x95, 0x08, 0x91, 0x02, 0xC0};

static int gwrite(const char *rel, const void *data, size_t len) {
  char path[256];
  snprintf(path, sizeof(path), GADGET "/%s", rel);
  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    if (errno != ENOENT)
      perror(path);
    return -1;
  }
  ssize_t n = write(fd, data, len);
  close(fd);
  if (n != (ssize_t)len) {
    perror(path);
    return -1;
  }
  return 0;
}
static int gset(const char *rel, const char *val) {
  return gwrite(rel, val, strlen(val));
}

static int gmkdir(const char *rel) {
  char path[256];
  snprintf(path, sizeof(path), GADGET "/%s", rel);
  if (mkdir(path, 0755) < 0 && errno != EEXIST) {
    perror(path);
    return -1;
  }
  return 0;
}

static void gadget_teardown(void) {
  gset("UDC", "\n"); /* unbind */
  unlink(GADGET "/configs/c.1/hid.usb0");
  rmdir(GADGET "/configs/c.1/strings/0x409");
  rmdir(GADGET "/configs/c.1");
  rmdir(GADGET "/functions/hid.usb0");
  rmdir(GADGET "/strings/0x409");
  rmdir(GADGET);
}

static int gadget_setup(void) {
  gadget_teardown(); /* clear leftovers from a crashed run */

  /* pick the first UDC */
  char udc[64] = {0};
  DIR *d = opendir("/sys/class/udc");
  if (d) {
    struct dirent *e;
    while ((e = readdir(d)))
      if (e->d_name[0] != '.') {
        snprintf(udc, sizeof(udc), "%s", e->d_name);
        break;
      }
    closedir(d);
  }
  if (!udc[0]) {
    fprintf(
        stderr,
        "No UDC found (is dtoverlay=dwc2 enabled and libcomposite loaded?)\n");
    return -1;
  }

  if (mkdir(GADGET, 0755) < 0) {
    perror(GADGET);
    return -1;
  }
  if (gset("idVendor", "0x0f0d") || gset("idProduct", "0x0092") ||
      gset("bcdDevice", "0x0100") || gset("bcdUSB", "0x0200") ||
      gmkdir("strings/0x409") || gset("strings/0x409/serialNumber", "0000") ||
      gset("strings/0x409/manufacturer", "HORI CO.,LTD.") ||
      gset("strings/0x409/product", "POKKEN CONTROLLER") ||
      gmkdir("configs/c.1") || gset("configs/c.1/MaxPower", "500") ||
      gmkdir("configs/c.1/strings/0x409") ||
      gset("configs/c.1/strings/0x409/configuration", "Config 1") ||
      gmkdir("functions/hid.usb0") ||
      gset("functions/hid.usb0/protocol", "0") ||
      gset("functions/hid.usb0/subclass", "0") ||
      gset("functions/hid.usb0/report_length", "8") ||
      gwrite("functions/hid.usb0/report_desc", report_desc,
             sizeof(report_desc)))
    return -1;

  if (symlink(GADGET "/functions/hid.usb0", GADGET "/configs/c.1/hid.usb0") <
      0) {
    perror("symlink");
    return -1;
  }
  if (gset("UDC", udc))
    return -1;
  fprintf(stderr, "[gadget] bound to %s\n", udc);
  return 0;
}

/* --------------------------------------------------------- pad state ---- */

struct report { /* 8 bytes, sent to the Switch */
  uint16_t buttons;
  uint8_t hat, lx, ly, rx, ry, vendor;
} __attribute__((packed));

struct pad {
  uint16_t buttons;
  int up, down, left, right; /* d-pad as buttons */
  int hat_x, hat_y;          /* d-pad as hat axes (-1,0,1) */
  uint8_t lx, ly, rx, ry;
};

static const struct {
  const char *name;
  uint16_t bit;
} btn[] = {{"Y", 1 << 0},     {"B", 1 << 1},       {"A", 1 << 2},
           {"X", 1 << 3},     {"L", 1 << 4},       {"R", 1 << 5},
           {"ZL", 1 << 6},    {"ZR", 1 << 7},      {"MINUS", 1 << 8},
           {"PLUS", 1 << 9},  {"LSTICK", 1 << 10}, {"RSTICK", 1 << 11},
           {"HOME", 1 << 12}, {"CAPTURE", 1 << 13}};

static uint8_t clamp8(long v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

static void apply(struct pad *p, const char *name, long v) {
  for (size_t i = 0; i < sizeof(btn) / sizeof(btn[0]); ++i) {
    if (strcmp(name, btn[i].name) == 0) {
      if (v)
        p->buttons |= btn[i].bit;
      else
        p->buttons &= (uint16_t)~btn[i].bit;
      return;
    }
  }
  if (!strcmp(name, "UP"))
    p->up = v != 0;
  else if (!strcmp(name, "DOWN"))
    p->down = v != 0;
  else if (!strcmp(name, "LEFT"))
    p->left = v != 0;
  else if (!strcmp(name, "RIGHT"))
    p->right = v != 0;
  else if (!strcmp(name, "HAT_X"))
    p->hat_x = v;
  else if (!strcmp(name, "HAT_Y"))
    p->hat_y = v;
  else if (!strcmp(name, "LX"))
    p->lx = clamp8(v);
  else if (!strcmp(name, "LY"))
    p->ly = clamp8(v);
  else if (!strcmp(name, "RX"))
    p->rx = clamp8(v);
  else if (!strcmp(name, "RY"))
    p->ry = clamp8(v);
  /* anything else (EV_SYN, FF_*, ...) is ignored */
}

static uint8_t hat_value(const struct pad *p) {
  int x = (p->right ? 1 : 0) - (p->left ? 1 : 0) + p->hat_x;
  int y = (p->down ? 1 : 0) - (p->up ? 1 : 0) + p->hat_y;
  x = x < -1 ? -1 : x > 1 ? 1 : x;
  y = y < -1 ? -1 : y > 1 ? 1 : y;
  if (x == 0 && y == -1)
    return 0; /* N  */
  if (x == 1 && y == -1)
    return 1; /* NE */
  if (x == 1 && y == 0)
    return 2; /* E  */
  if (x == 1 && y == 1)
    return 3; /* SE */
  if (x == 0 && y == 1)
    return 4; /* S  */
  if (x == -1 && y == 1)
    return 5; /* SW */
  if (x == -1 && y == 0)
    return 6; /* W  */
  if (x == -1 && y == -1)
    return 7; /* NW */
  return 8;   /* neutral */
}

/* Parses "<device>: <name> = <value>\n" as sent by input_reader.c */
static void handle_message(char *msg, struct pad *p) {
  char *eq = strrchr(msg, '=');
  if (!eq)
    return;
  long value = strtol(eq + 1, NULL, 10);

  char *end = eq;
  while (end > msg && end[-1] == ' ')
    --end;
  *end = '\0';

  char *name = NULL;
  for (char *q = end - 1; q > msg; --q) { /* last ": " before the name */
    if (q[0] == ' ' && q[-1] == ':') {
      name = q + 1;
      q[-1] = '\0';
      break;
    }
  }
  if (!name)
    return;

  /* The DualSense also exposes motion-sensor and touchpad devices that emit
     ABS_X/Y/... events; ignore them so they don't overwrite the sticks. */
  if (strstr(msg, "Motion Sensors") || strstr(msg, "Touchpad"))
    return;

  apply(p, name, value);
}

static void send_report(int hidg, const struct pad *p) {
  struct report r = {.buttons = p->buttons,
                     .hat = hat_value(p),
                     .lx = p->lx,
                     .ly = p->ly,
                     .rx = p->rx,
                     .ry = p->ry,
                     .vendor = 0};
  (void)!write(hidg, &r, sizeof(r)); /* EAGAIN etc.: next tick retries */
  uint8_t junk[64];
  while (read(hidg, junk, sizeof(junk)) > 0) {
  } /* discard host output reports */
}

/* -------------------------------------------------------------- main ---- */

static volatile sig_atomic_t running = 1;
static void on_signal(int s) {
  (void)s;
  running = 0;
}

int main(void) {
  struct sigaction sa = {0};
  sa.sa_handler = on_signal; /* no SA_RESTART: interrupt ppoll */
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  if (gadget_setup() < 0) {
    gadget_teardown();
    return 1;
  }

  int hidg = -1;
  for (int i = 0; i < 50 && hidg < 0; ++i) { /* wait for udev to create it */
    hidg = open("/dev/hidg0", O_RDWR | O_NONBLOCK);
    if (hidg < 0)
      usleep(100000);
  }
  if (hidg < 0) {
    perror("open /dev/hidg0");
    gadget_teardown();
    return 1;
  }

  int sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SERVER, sizeof(addr.sun_path) - 1);
  unlink(SERVER);
  if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind " SERVER);
    gadget_teardown();
    return 1;
  }

  struct pad st = {.lx = 128, .ly = 128, .rx = 128, .ry = 128};
  struct pollfd pfd = {.fd = sock, .events = POLLIN};
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  fprintf(stderr, "[server] ready\n");

  while (running) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long wait =
        (next.tv_sec - now.tv_sec) * 1000000000L + (next.tv_nsec - now.tv_nsec);
    if (wait < 0)
      wait = 0;
    struct timespec to = {0, wait};

    if (ppoll(&pfd, 1, &to, NULL) > 0 && (pfd.revents & POLLIN)) {
      char buf[256];
      ssize_t n;
      while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        handle_message(buf, &st);
      }
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > next.tv_sec ||
        (now.tv_sec == next.tv_sec && now.tv_nsec >= next.tv_nsec)) {
      send_report(hidg, &st);
      next = now;
      next.tv_nsec += TICK_NS;
      if (next.tv_nsec >= 1000000000L) {
        next.tv_sec++;
        next.tv_nsec -= 1000000000L;
      }
    }
  }

  close(sock);
  unlink(SERVER);
  close(hidg);
  gadget_teardown();
  return 0;
}

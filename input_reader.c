// Author: AJ Tomalino
// File: input_reader.c
// Purpose: read and write controller inputs using the EVDEV linux kernel API
// Compilation: gcc input_reader.c dyArray.c cJSON.c

// TODO:

// Libraries
#include "cJSON.h"
#include "shared/protocol.hpp"
#include "shared/sha256.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace ns;

// ---------------------------------------------------------------- state ----

struct Device {
  int fd;
  char name[128];
  input_absinfo abs[ABS_CNT]; // per-axis min/max, for scaling to 0..255
};
static std::vector<Device *> open_devs;

struct PadState {
  uint16_t buttons = 0;
  bool up = false, down = false, left = false, right = false;
  int hat_x = 0, hat_y = 0; // from ABS_HAT0X / ABS_HAT0Y (-1,0,1)
  uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
};

static volatile sig_atomic_t running = 1;
static void on_signal(int) { running = 0; }

// ------------------------------------------------------- packet sending ----

static uint8_t hmac_key[32];
static uint32_t seq = 0;

static uint8_t hat_from(const PadState &s) {
  int x = std::clamp((s.right ? 1 : 0) - (s.left ? 1 : 0) + s.hat_x, -1, 1);
  int y = std::clamp((s.down ? 1 : 0) - (s.up ? 1 : 0) + s.hat_y, -1, 1);
  if (x == 0 && y == -1)
    return HAT_N;
  if (x == 1 && y == -1)
    return HAT_NE;
  if (x == 1 && y == 0)
    return HAT_E;
  if (x == 1 && y == 1)
    return HAT_SE;
  if (x == 0 && y == 1)
    return HAT_S;
  if (x == -1 && y == 1)
    return HAT_SW;
  if (x == -1 && y == 0)
    return HAT_W;
  if (x == -1 && y == -1)
    return HAT_NW;
  return HAT_NEUTRAL;
}

static void send_packet(int sock, const PadState *s, uint8_t flags) {
  Packet pkt{};
  pkt.flags = flags;
  pkt.seq = seq++;
  pkt.ts_us = now_us();
  pkt.report.reset();
  if (s) {
    HIDReport &p = pkt.report.p1;
    p.input.buttons = s->buttons;
    p.input.hat = hat_from(*s);
    p.input.lx = s->lx;
    p.input.ly = s->ly;
    p.input.rx = s->rx;
    p.input.ry = s->ry;
    p.input.vendor |= EXT_PAD_PRESENT; // marks pad 1 as present
  }
  uint8_t full[32];
  hmac_sha256(
      std::span(hmac_key, 32),
      std::span(reinterpret_cast<const uint8_t *>(&pkt), PACKET_AUTH_SIZE),
      std::span<uint8_t, 32>(full));
  std::memcpy(pkt.hmac, full, HMAC_TAG_SIZE);
  send(sock, &pkt, sizeof(pkt), 0);
}

// ------------------------------------------------------ input -> state ----

static uint8_t scale_axis(const input_absinfo &a, int v) {
  long range = (long)a.maximum - a.minimum;
  if (range <= 0)
    return 128;
  long n = ((long)v - a.minimum) * 255 / range;
  return (uint8_t)std::clamp(n, 0L, 255L);
}

static void set_bit(PadState &s, uint16_t bit, bool on) {
  if (on)
    s.buttons |= bit;
  else
    s.buttons &= ~bit;
}

static void apply_input(PadState &s, const std::string &name,
                        const input_event &ev, const Device &dev) {
  const bool is_abs = (ev.type == EV_ABS);
  const uint8_t scaled =
      is_abs && ev.code < ABS_CNT ? scale_axis(dev.abs[ev.code], ev.value) : 0;
  // buttons: key events are pressed when nonzero; analog triggers past ~40%
  const bool pressed = is_abs ? scaled > 100 : ev.value != 0;

  if (name == "A")
    set_bit(s, BTN_A, pressed);
  else if (name == "B")
    set_bit(s, BTN_B, pressed);
  else if (name == "X")
    set_bit(s, BTN_X, pressed);
  else if (name == "Y")
    set_bit(s, BTN_Y, pressed);
  else if (name == "L")
    set_bit(s, BTN_L, pressed);
  else if (name == "R")
    set_bit(s, BTN_R, pressed);
  else if (name == "ZL")
    set_bit(s, BTN_ZL, pressed);
  else if (name == "ZR")
    set_bit(s, BTN_ZR, pressed);
  else if (name == "MINUS")
    set_bit(s, BTN_MINUS, pressed);
  else if (name == "PLUS")
    set_bit(s, BTN_PLUS, pressed);
  else if (name == "LSTICK")
    set_bit(s, BTN_LSTICK, pressed);
  else if (name == "RSTICK")
    set_bit(s, BTN_RSTICK, pressed);
  else if (name == "HOME")
    set_bit(s, BTN_HOME, pressed);
  else if (name == "CAPTURE")
    set_bit(s, BTN_CAPTURE, pressed);
  else if (name == "UP")
    s.up = pressed;
  else if (name == "DOWN")
    s.down = pressed;
  else if (name == "LEFT")
    s.left = pressed;
  else if (name == "RIGHT")
    s.right = pressed;
  else if (name == "HAT_X")
    s.hat_x = ev.value;
  else if (name == "HAT_Y")
    s.hat_y = ev.value;
  else if (name == "LX")
    s.lx = scaled;
  else if (name == "LY")
    s.ly = scaled;
  else if (name == "RX")
    s.rx = scaled;
  else if (name == "RY")
    s.ry = scaled;
}

// Same lookup as before: JSON { "<type>": { "<code>": "NAME" | ["NAME", ...] }
// }
static bool map_input_event(cJSON *json, const input_event &ev,
                            std::string &out) {
  char tk[16], ck[16];
  snprintf(tk, sizeof(tk), "%u", ev.type);
  snprintf(ck, sizeof(ck), "%u", ev.code);
  cJSON *t = cJSON_GetObjectItemCaseSensitive(json, tk);
  if (!cJSON_IsObject(t))
    return false;
  cJSON *m = cJSON_GetObjectItemCaseSensitive(t, ck);
  if (cJSON_IsString(m)) {
    out = m->valuestring;
  } else if (cJSON_IsArray(m)) {
    cJSON *n = cJSON_GetArrayItem(m, 0);
    if (!cJSON_IsString(n))
      return false;
    out = n->valuestring;
  } else {
    return false;
  }
  if (out.size() >= 2 && out.front() == '(' && out.back() == ')')
    out = out.substr(1, out.size() - 2);
  return true;
}

// ------------------------------------------------------- device handling ----

static bool open_joysticks(int epoll_fd) {
  DIR *dir = opendir("/dev/input/");
  if (!dir) {
    perror("Could not open /dev/input/");
    return false;
  }
  while (dirent *e = readdir(dir)) {
    unsigned n;
    if (sscanf(e->d_name, "event%u", &n) != 1)
      continue;
    std::string path = std::string("/dev/input/") + e->d_name;
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK); // read-only is enough
    if (fd < 0)
      continue;
    auto *d = new Device{};
    d->fd = fd;
    ioctl(fd, EVIOCGNAME(sizeof(d->name)), d->name);
    for (int c = 0; c < ABS_CNT; ++c)
      ioctl(fd, EVIOCGABS(c), &d->abs[c]);
    epoll_event ee{};
    ee.events = EPOLLIN;
    ee.data.ptr = d;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ee) < 0) {
      perror("epoll_ctl add");
      close(fd);
      delete d;
      continue;
    }
    open_devs.push_back(d);
  }
  closedir(dir);
  return true;
}

static void close_joysticks(int epoll_fd) {
  for (Device *d : open_devs) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, d->fd, nullptr);
    close(d->fd);
    delete d;
  }
  open_devs.clear();
}

// ------------------------------------------------------------------ main ----

int main() {
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  // JSON config
  std::ifstream f("data.json");
  if (!f) {
    fprintf(stderr, "Unable to open data.json\n");
    return 1;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string text = ss.str();
  cJSON *json = cJSON_Parse(text.c_str());
  if (!json) {
    fprintf(stderr, "Invalid data.json\n");
    return 1;
  }

  // UDP socket to ns-backend. One socket for the whole run.
  derive_key(DEFAULT_SECRET, hmac_key);
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(DEFAULT_PORT);
  inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
  if (connect(sock, (sockaddr *)&dest, sizeof(dest)) < 0) {
    perror("connect");
    return 1;
  }

  // epoll + hotplug
  int epoll_fd = epoll_create1(0);
  open_joysticks(epoll_fd);
  int inotify_fd = inotify_init1(IN_NONBLOCK);
  inotify_add_watch(inotify_fd, "/dev/input",
                    IN_ATTRIB | IN_CREATE | IN_DELETE);
  static char inotify_ID;
  epoll_event ie{};
  ie.events = EPOLLIN;
  ie.data.ptr = &inotify_ID;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ie);

  PadState state;
  uint64_t last_send = 0;
  const uint64_t interval_us = PRO_UDP_INTERVAL_MS * 1000ull;

  while (running) {
    epoll_event event{};
    int n = epoll_wait(epoll_fd, &event, 1, 2); // wake at least every 2 ms
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("epoll_wait");
      break;
    }

    if (n == 1) {
      if (event.data.ptr == &inotify_ID) {
        char buf[4096];
        while (read(inotify_fd, buf, sizeof(buf)) > 0) {
        }
        close_joysticks(epoll_fd);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50)); // let udev settle
        open_joysticks(epoll_fd);
      } else {
        Device *dev = static_cast<Device *>(event.data.ptr);
        input_event ev;
        ssize_t b = read(dev->fd, &ev, sizeof(ev));
        if (b == (ssize_t)sizeof(ev)) {
          std::string name;
          if (map_input_event(json, ev, name))
            apply_input(state, name, ev, *dev);
        }
        // EAGAIN / ENODEV: ignore; inotify will refresh the device list
      }
    }

    // Send the full state at a steady rate, whether or not anything changed.
    uint64_t now = now_us();
    if (now - last_send >= interval_us) {
      send_packet(sock, &state, FLAG_NONE);
      last_send = now;
    }
  }

  // Free our slot on the server
  for (int i = 0; i < 3; ++i) {
    send_packet(sock, nullptr, FLAG_RESET | FLAG_DISCONNECT);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  close_joysticks(epoll_fd);
  cJSON_Delete(json);
  return 0;
}

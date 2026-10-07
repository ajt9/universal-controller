// Author: AJ Tomalino
// File: input_reader.c
// Purpose: read and write controller inputs using the EVDEV linux kernel API
// Compilation: gcc input_reader.c
// Template from github/MysteriousJ/Joystick-Input-Examples

// Libraries

#include <dirent.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Compiler Macros

#define MAXBUTTONS 32
#define MAXAXES 32
#define MAXFILENAMESIZE 128

// Structs

typedef struct {
  int min;
  int max;
  int value;
} Axis;

struct JoystickState {
  bool connected;
  unsigned int deviceNumber;
  int file;
  bool buttons[MAXBUTTONS];
  Axis axes[MAXAXES];
  char name[128];
  bool hasRumble;
  short rumbleEffectID;
};

struct Joysticks {
  unsigned int count;
  struct JoystickState *states;
};

// Function Prototypes

int open_joysticks(struct Joysticks *);
void close_joystick(struct Joysticks *);
void read_joystick(struct JoystickState *);
void set_joystick_rumble(struct JoystickState *, short, short);

// Main

int main() {
  struct Joysticks js = {0};
  int ret = open_joysticks(&js);
  if (ret == EXIT_FAILURE) {
    return ret;
  } // if failed to open joystick

  // Add hook to check /dev/input/
  int deviceChangeNotify = inotify_init1(IN_NONBLOCK);
  inotify_add_watch(deviceChangeNotify, "/dev/input", IN_ATTRIB);

  while (1) {
    // read hook to check if /dev/input/ changed
    char buffer[4096];
    if (read(deviceChangeNotify, buffer, sizeof(buffer)) != -1) {
      close_joystick(&js);
      open_joysticks(&js);
    }

    // update controller states
    for (int i = 0; i < js.count; i++) {
      struct JoystickState *j = &js.states[i];
      if (j->connected) {
        // read
        read_joystick(j);

        // print
        fprintf(stdout, "%s: ", j->name);
        for (int i = 0; i < MAXAXES; i++) {
          if (!(j->axes[i].max == 0 && j->axes[i].min == 0)) {
            fprintf(stdout, "Axis %d: %d ", i, j->axes[i].value);
          }
        }
        fprintf(stdout, "Buttons: ");
        for (int i = 0; i < MAXBUTTONS; i++) {
          if (j->buttons[i]) {
            fprintf(stdout, "%d ", i);
          }
        }
        fprintf(stdout, "\n");
      }
    }
    fflush(stdout);
    usleep(16000);
  }
}

// Functions

int open_joysticks(struct Joysticks *js) {
  // open /dev/input/
  DIR *dir = opendir("/dev/input/");
  if (dir == NULL) {
    perror("Could not open /dev/input/");
    return EXIT_FAILURE;
  }

  // iterate through /dev/input/
  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    // get only event files
    unsigned int ev_num;
    if (sscanf(entry->d_name, "event%u", &ev_num) == 1) {
      char path[MAXFILENAMESIZE];
      sprintf(path, "/dev/input/%s", entry->d_name);
      int fd = open(path, O_RDWR | O_NONBLOCK);
      if (fd != -1) {
        // init state
        struct JoystickState j = {0};
        j.deviceNumber = ev_num;
        j.file = fd;
        j.connected = true;

        // get name
        ioctl(fd, EVIOCGNAME(sizeof(j.name)), j.name);

        // init axes
        for (int i = 0; i < MAXAXES; i++) {
          struct input_absinfo axisInfo;
          if (ioctl(fd, EVIOCGABS(i), &axisInfo) != -1) {
            j.axes[i].min = axisInfo.minimum;
            j.axes[i].max = axisInfo.maximum;
          }
        }

        // it is unnecessary to init buttons

        // init rumble
        // ...

        // add to states
        bool previously_connected = false;
        for (int i = 0; i < js->count; i++) {
          if (js->states[i].deviceNumber == ev_num) {
            js->states[i] = j;
            previously_connected = true;
          }
        }
        if (!previously_connected) {
          js->count++;
          js->states = (struct JoystickState *)realloc(
              js->states, sizeof(struct JoystickState) * js->count);
          js->states[js->count - 1] = j;
        }
      }
    }
  }
  for (int i = 0; i < js->count; i++) {
    if (js->states[i].connected) {
      printf("Connected: %s\n", js->states[i].name);
    }
  }

  fflush(stdout);

  return EXIT_SUCCESS;
}

void close_joystick(struct Joysticks *js) {
  for (int i = 0; i < js->count; i++) {
    if (js->states->connected) {
      close(js->states[i].file);
      js->states[i].connected = false;
      printf("Disconnected: %s\n", js->states[i].name);
    }
  }
}

void read_joystick(struct JoystickState *j) {
  struct input_event ev;
  while (read(j->file, &ev, sizeof(ev)) > 0) {
    if (ev.type == EV_KEY && ev.code >= BTN_JOYSTICK && ev.code <= BTN_THUMBR) {
      j->buttons[ev.code - 0x120] = ev.value;
    }
    if (ev.type == EV_ABS && ev.code <= ABS_TOOL_WIDTH) {
      j->axes[ev.code].value = ev.value;
    }
  }
}

void set_joystick_rumble(struct JoystickState *j, short weak_rumble,
                         short strong_rumble) {}

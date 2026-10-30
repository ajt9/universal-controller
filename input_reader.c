// Author: AJ Tomalino
// File: input_reader.c
// Purpose: read and write controller inputs using the EVDEV linux kernel API
// Compilation: gcc input_reader.c dyArray.c
// Template from github/MysteriousJ/Joystick-Input-Examples

// TODO:
//  Parse the JSON Config (ln 78)
//  Map input_event structs to what they are in the JSON (ln 135)
//  Send the mapping to the socket instead of the raw event (ln 135)

// Libraries

#include "dyArray.h"
#include <bits/types/struct_timeval.h>
#include <dirent.h>
// #include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// #include <string.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

// Compiler Macros

#define MAXFILENAMESIZE 128
#define CLIENT "client.sock"
#define SERVER "server.sock"

// Global Variables

dyArray input_queue;

// Structs

typedef struct {
  int ev_fd;
  pthread_t tid;
} thread_event;

typedef struct {
  char name[128];
  struct input_event ev;
} hid_input_event;

// Function Prototypes

void *runner(void *);
int open_joysticks(thread_event *);
// void set_joystick_rumble(struct JoystickState *, short, short);

bool pq_enqueue(dyArray *, hid_input_event *);
hid_input_event pq_dequeue(dyArray *);
int timeval_cmp(struct timeval a, struct timeval b);

// Main

int main() {
  // Initialize open controllers
  thread_event *open_events;
  unsigned int num_open_events = open_joysticks(open_events);
  if (num_open_events < 0) {
    return EXIT_FAILURE;
  }

  // Initialize JSON Parser

  // Initialize socket
  struct sockaddr_un addr;
  struct sockaddr_un from;
  int socket_fd;
  char message[256];
  if ((socket_fd = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0) {
    perror("Failed to open socket");
    return EXIT_FAILURE;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, CLIENT);
  unlink(CLIENT);
  if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Bind failed");
    return EXIT_FAILURE;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, SERVER);
  if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("Connection failed");
    return EXIT_FAILURE;
  }

  // // Send test message
  // strcpy(message, "Test");
  // if (send(socket_fd, message, strlen(message) + 1, 0) == -1) {
  //   perror("Send failed");
  //   return EXIT_FAILURE;
  // }

  // Initialize queue
  input_queue = array_init(sizeof(hid_input_event));

  // Add hook to check /dev/input/
  int deviceChangeNotify = inotify_init1(IN_NONBLOCK);
  int deviceWatch =
      inotify_add_watch(deviceChangeNotify, "/dev/input", IN_ATTRIB);

  while (1) {
    // read hook to check if /dev/input/ changed
    char buffer[4096];
    if (read(deviceChangeNotify, buffer, sizeof(buffer)) != -1) {
      // close_joystick(open_events, num_open_events);
      free(open_events);
      num_open_events = open_joysticks(open_events);
      // printf("%d\n", num_open_events);
    }

    hid_input_event hid_ev;
    if (input_queue.len > 0) {
      hid_ev = pq_dequeue(&input_queue);
      struct input_event ev = hid_ev.ev;

      // Do lookup from input_event to JSON here
      // Change message to the result of the lookup rather than the raw event
      sprintf(message, "%s: %d.%d, %x, %x, %d\n", hid_ev.name,
              (int)ev.time.tv_sec, (int)ev.time.tv_usec, ev.type, ev.code,
              ev.value);

      // Send event to server
      if (send(socket_fd, message, strlen(message) + 1, 0) == -1) {
        perror("Send failed");
        return EXIT_FAILURE;
      }
    }
  }

  // technically this code is never executed but its good practice
  // may do a signal intercept to make SIG_INT execute this

  array_free(&input_queue);

  for (int i = 0; i < num_open_events; i++) {
    pthread_cancel(open_events[i].tid);
    close(open_events[i].ev_fd);
  }

  free(open_events);

  inotify_rm_watch(deviceChangeNotify, deviceWatch);

  return 0;
}

// Functions

void *runner(void *args) {
  // char name[128];
  hid_input_event hid_ev;
  int fd = *(int *)args;
  ioctl(fd, EVIOCGNAME(128), hid_ev.name);
  printf("%s\n", hid_ev.name);
  fflush(stdout);
  while (1) {
    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) > 0) {
      hid_ev.ev = ev;
      pq_enqueue(&input_queue, &hid_ev);
      usleep(16000);
    }
  }
  close(fd);

  pthread_exit(0);
}

int open_joysticks(thread_event *open_threads) {
  // open /dev/input/
  DIR *dir = opendir("/dev/input/");
  if (dir == NULL) {
    perror("Could not open /dev/input/");
    return -1;
  }

  open_threads = (thread_event *)malloc(sizeof(thread_event));
  unsigned int n = 0;

  // iterate through /dev/input/
  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    // get only event files
    unsigned int ev_num;
    if (sscanf(entry->d_name, "event%u", &ev_num) == 1) {
      char path[MAXFILENAMESIZE];
      sprintf(path, "/dev/input/%s", entry->d_name);
      int fd = open(path, O_RDWR | O_NONBLOCK);
      // printf("Found: %s\n", path);
      // opens any device with read-write permission
      // fails if else
      if (fd != -1) {
        // printf("Opened: %s\n", path);
        open_threads =
            (thread_event *)realloc(open_threads, ++n * sizeof(thread_event));
        if (open_threads == NULL) {
          perror("Realloc failed");
          exit(EXIT_FAILURE);
        }
        open_threads[n - 1].ev_fd = fd;
      }
    }
  }
  for (int i = 0; i < n; i++) {
    open_threads[i].tid = pthread_create(&open_threads[i].tid, NULL, runner,
                                         &open_threads[i].ev_fd);
  }

  // fflush(stdout);

  return n;
}

// void set_joystick_rumble(struct JoystickState *j, short weak_rumble,
//                          short strong_rumble) {}

bool pq_enqueue(dyArray *dy, hid_input_event *ev) {
  // O(n) insert
  // sort by highest (most recent) time
  for (int i = 0; i < dy->len; i++) {
    if (timeval_cmp(ev->ev.time,
                    ((hid_input_event *)array_at(dy, i))->ev.time) > 0) {
      return array_insert(dy, ev, i);
    }
  }
  return array_push(dy, ev);
}

hid_input_event pq_dequeue(dyArray *dy) {
  // O(n) remove
  // O(1) search
  hid_input_event ret = *(hid_input_event *)array_at(dy, 0);
  array_rmv(dy, 0);
  return ret;
}

// return  <0 if a < b
//         0  if a == b
//         >0 if a > b
int timeval_cmp(struct timeval a, struct timeval b) {
  struct timeval res;
  res.tv_sec = a.tv_sec - b.tv_sec;
  res.tv_usec = a.tv_usec - b.tv_usec;
  if (res.tv_sec == 0) {
    return res.tv_usec;
  }
  return res.tv_sec;
}

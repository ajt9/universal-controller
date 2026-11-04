// Author: AJ Tomalino
// File: input_reader.c
// Purpose: read and write controller inputs using the EVDEV linux kernel API
// Compilation: gcc input_reader.c dyArray.c

// TODO:
// Translate events via cJSON (ln 153)

// Libraries

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "cJSON.h"
#include "dyArray.h"

// Macros

#define MAXFILENAMESIZE 128
#define MAXDEVICENAMESIZE 128
#define CLIENT "client.sock"
#define SERVER "server.sock"

// Global Variables

dyArray open_events; // hold device pointers
                     // in close_events for each element just close each fd and
                     // free the pointer

// Structs

struct device {
  int fd;
  char name[MAXDEVICENAMESIZE];
};

// Function Prototypes

bool open_joysticks(int);
bool close_joysticks(int);

bool map_input_event(cJSON *json, struct input_event *ev, char *input_name, size_t name_size);

// Main

int main() {
  // Create epoll instance
  const int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("Could not create epoll");
    return EXIT_FAILURE;
  }

  // Initialize Open Joysticks
  open_events = array_init(sizeof(struct device *));
  open_joysticks(epoll_fd);

  // Initialize JSON Parser
  FILE *fp = fopen("data.json", "r");
  if (fp == NULL) {
    printf("Error: Unable to open the file.\n");
    return 1;
  }

  char buffer[2048];
  int len = fread(buffer, 1, sizeof(buffer), fp);
  fclose(fp);

  cJSON *json = cJSON_Parse(buffer);
  if (json == NULL) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      printf("Error: %s\n", error_ptr);
    }
    cJSON_Delete(json);
    return 1;
  }

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

  // Add hook to check /dev/input/
  int inotify_fd = inotify_init1(IN_NONBLOCK);
  int deviceWatch = inotify_add_watch(inotify_fd, "/dev/input", IN_ATTRIB);

  static char inotify_ID; // we know if an event is related to inotify because
                          // its data will hold this pointer
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd,
            &(struct epoll_event){.events = EPOLLIN, .data.ptr = &inotify_ID});

  // Read Forever
  while (1) {
    // Wait for IO
    struct epoll_event event = {};
    if (epoll_wait(epoll_fd, &event, 1, -1) < 0) {
      perror("Failed while waiting for I/O");
      return EXIT_FAILURE;
    }
    // printf("epoll recieved!\n");

    if (event.data.ptr == &inotify_ID) {
      char buffer[4096];
      while (read(inotify_fd, buffer, sizeof(buffer)) > 0)
        ;
      // printf("Devices Changed\n");
      close_joysticks(epoll_fd);
      open_joysticks(epoll_fd);
      continue;
    }

    hid_input_event hid_ev;
    if (input_queue.len > 0) {
        hid_ev = pq_dequeue(&input_queue);
        struct input_event ev = hid_ev.ev;
        
        // Do lookup from input_event to JSON here
        char input_name[128];
        if (map_input_event(json, &ev, input_name, sizeof(input_name))) {
            //send message of the result of the lookup rather than the raw event
            snprintf(message, sizeof(message), "%s: %s = %d\n", hid_ev.name, input_name, ev.value);
            // Send event to server
            if (send(socket_fd, message, strlen(message) + 1, 0) == -1) {
                perror("Send failed");
                return EXIT_FAILURE;
            }// end if
        }// end if
    }//end if
    if (open_events.len > 0) {
      // Read from IO fd
      struct input_event ev;
      struct device dev = *(struct device *)event.data.ptr;
      // printf("fd outside open: %d\n", dev.fd);
      // printf("%s\n", dev.name);
      const ssize_t bytes = read(dev.fd, &ev, sizeof(ev));
      if (bytes < 0) {
        fprintf(stderr, "Failed to read I/O from %s: ", dev.name);
        perror("");
        return EXIT_FAILURE;
        // }
      } else if (bytes != sizeof(ev)) {
        continue;
      }

      // Translate event here

      // Send Message
      char message[256];
      sprintf(message, "%s: %d.%d, %d, %d, %d\n", dev.name, (int)ev.time.tv_sec,
              (int)ev.time.tv_usec, ev.type, ev.code, ev.value);
      // printf("%s", message);
      if (send(socket_fd, message, strlen(message) + 1, 0) == -1) {
        perror("Send failed");
        return EXIT_FAILURE;
      }
    }
  }
  //
  // if (close(epoll_fd) < 0) {
  //   perror("Could not destroy epoll");
  // }
  //
  return EXIT_SUCCESS;
}

// Functions

bool map_input_event(cJSON *json, struct input_event *ev, char *input_name, size_t name_size) {

  char type_key[16];
  char code_key[16];

  // Convert event type and code into JSON keys
  snprintf(type_key, sizeof(type_key), "%u", ev->type);
  snprintf(code_key, sizeof(code_key), "%u", ev->code);

  // Find the event type in the JSON configuration
  cJSON *type_object =
      cJSON_GetObjectItemCaseSensitive(json, type_key);

  if (!cJSON_IsObject(type_object)) {
    return false;
  }//end if

  // Find the specific event code
  cJSON *mapping = cJSON_GetObjectItemCaseSensitive(type_object, code_key);
  if (mapping == NULL) {
    return false;
  }//end if

  // Handle button and force-feedback mappings
  if (cJSON_IsString(mapping)) {
    snprintf(input_name, name_size, "%s", mapping->valuestring);
    // Remove parentheses from button names
    size_t len = strlen(input_name);
    if (len >= 2 &&
        input_name[0] == '(' &&
        input_name[len - 1] == ')') {
      memmove(input_name, input_name + 1, len - 2);
      input_name[len - 2] = '\0';
    }//end if

    return true;
  }//end if

  // Handle analog inputs (ABS_X, ABS_Y, etc.)
  if (cJSON_IsArray(mapping)) {

    cJSON *axis_name = cJSON_GetArrayItem(mapping, 0);

    if (!cJSON_IsString(axis_name)) {
      return false;
    }//end if
    snprintf(input_name, name_size, "%s", axis_name->valuestring);
    return true;
  }//end if
  return false;
}//end map_input_event


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
bool open_joysticks(int epoll_fd) {
  // open /dev/input/
  DIR *dir = opendir("/dev/input/");
  if (dir == NULL) {
    perror("Could not open /dev/input/");
    return -1;
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
      // printf("Found: %s\n", path);
      // opens any device with read-write permission
      // fails if else
      if (fd != -1) {
        // printf("fd in open: %d\n", fd);

        // add fd to epoll
        struct device *dev = malloc(sizeof(*dev));
        dev->fd = fd;
        ioctl(fd, EVIOCGNAME(MAXDEVICENAMESIZE), dev->name);
        struct epoll_event event = {.events = EPOLLIN, .data.ptr = dev};
        ioctl(fd, EVIOCGNAME(128), dev->name);
        // printf("%s\n", (char *)event.data.ptr);
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
          fprintf(stderr, "Failed to add %s to epoll: ", path);
          perror("");
          return false;
        }
        // printf("Opened: %s | %s\n", path, dev->name);
        // add dev to open array
        array_push(&open_events, &dev);
      }
    }
  }

  // for (int i = 0; i < open_events.len; i++) {
  //   struct device *dev = *(struct device **)array_at(&open_events, i);
  //   printf("Opened %s %d\n", dev->name, dev->fd);
  // }

  return true;
}

bool close_joysticks(int epoll_fd) {
  while (open_events.len > 0) {
    struct device *dev = *(struct device **)array_at(&open_events, 0);
    // printf("Closing %s %d\n", dev->name, dev->fd);
    array_rmv(&open_events, 0);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dev->fd, NULL) < 0) {
      perror("Failed to remove event");
      return false;
    }
    if (close(dev->fd) < 0) {
      perror("Failed to close event");
    }
    free(dev);
  }
  return true;
}

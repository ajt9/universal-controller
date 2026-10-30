// Author: AJ Tomalino
// File: server.c
// Purpose: Maintain a controller state for the target console to communicate
// with, get updates from input_reader.c Compilation: gcc server.c
// Template from https://github.com/denehs/unix-domain-socket-example/

// TODO:
// Maintain controller state
// Use messages from socket to update controller state
// Let controller state communicate with target console
// Have controller state be compliant with target console's protocol

// Libraries

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Compiler Macros

#define CLIENT "client.sock"
#define SERVER "server.sock"
#define SIZE 256

// Global Variables

// Function Prototypes

// Main

int main() {
  // Init socket
  int fd;
  struct sockaddr_un addr;
  struct sockaddr_un from;
  char buffer[SIZE];

  socklen_t fromlen = sizeof(from);
  int len;

  if ((fd = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0) {
    perror("Failed to create socket");
    return EXIT_FAILURE;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, SERVER);
  unlink(SERVER);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Bind Failed");
    return EXIT_FAILURE;
  }

  // Read from socket
  while ((len = recvfrom(fd, buffer, SIZE, 0, (struct sockaddr *)&from,
                         &fromlen)) > 0) {
    printf("%s", buffer);
    // printf("Message Received\n");
  }

  if (fd >= 0) {
    close(fd);
  }

  return 0;
}

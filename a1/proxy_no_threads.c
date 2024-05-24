#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int serverSocket;

void handle_signal(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    close(serverSocket);
    exit(0);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: %s port\n", argv[0]);
    exit(1);
  }

  int port = atoi(argv[1]);
  int communicationSocket;
  char request[BUFFER_SIZE];
  char requestCopy[BUFFER_SIZE];
  struct sockaddr_in serverAddr;
  struct sockaddr_storage serverStorage;
  socklen_t addr_size;

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  serverSocket = socket(PF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    perror("Error creating socket");
    exit(1);
  }

  int opt = 1;
  if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    perror("Error setting SO_REUSEADDR");
    exit(1);
  }

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(port);
  serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  memset(serverAddr.sin_zero, 0, sizeof serverAddr.sin_zero);

  if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) <
      0) {
    perror("Error binding socket");
    exit(1);
  }

  if (listen(serverSocket, 5) == 0) {
    printf("Server started successfully. Listening on port %d\n", port);
  } else {
    perror("Error listening");
    exit(1);
  }

  while (1) {
    addr_size = sizeof(serverStorage);
    communicationSocket =
        accept(serverSocket, (struct sockaddr *)&serverStorage, &addr_size);
    if (communicationSocket < 0) {
      perror("Error accepting connection");
      continue;
    }

    if (read(communicationSocket, request, BUFFER_SIZE - 1) < 0) {
      perror("Error reading request");
      close(communicationSocket);
      continue;
    }
    printf("Received request\n");
    strcpy(requestCopy, request);

    char *token = strtok(requestCopy, "\r\n");
    int i = 0;
    while (i != 1) {
      token = strtok(NULL, "\r\n");
      i += 1;
    }

    char *name = strtok(token, " ");
    name = strtok(NULL, " ");

    struct hostent *host = gethostbyname(name);
    if (host == NULL) {
      perror("Error resolving host");
      close(communicationSocket);
      continue;
    }
    struct in_addr ipaddr = *(struct in_addr *)(host->h_addr);

    int clientSocket;
    struct sockaddr_in clientAddr;
    char response[BUFFER_SIZE];

    clientSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
      perror("Failed to create socket");
      close(communicationSocket);
      continue;
    }

    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(80);
    clientAddr.sin_addr.s_addr = inet_addr(inet_ntoa(ipaddr));
    memset(clientAddr.sin_zero, 0, sizeof clientAddr.sin_zero);

    if (connect(clientSocket, (struct sockaddr *)&clientAddr,
                sizeof(clientAddr)) < 0) {
      perror("Error connecting to server");
      close(clientSocket);
      close(communicationSocket);
      continue;
    }

    if (send(clientSocket, request, strlen(request), 0) < 0) {
      perror("Error sending request");
      close(clientSocket);
      close(communicationSocket);
      continue;
    }
    printf("Sent request\n");

    ssize_t n;
    while ((n = read(clientSocket, response, BUFFER_SIZE)) > 0) {
      if (write(communicationSocket, response, n) < 0) {
        perror("Error writing to communicationSocket");
        break;
      }
      printf("Received and sent response\n");
      memset(response, 0, n);
    }
    if (n < 0) {
      perror("Error reading from clientSocket");
    }

    close(communicationSocket);
    close(clientSocket);
  }

  close(serverSocket);
  return 0;
}

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Maximum number of simultaneous connections
// (needed because large sites create lots of requests)
#define MAX_CLIENTS 20

int server_fd;                         // Global to access across functions
pthread_t client_threads[MAX_CLIENTS]; // Array to store client threads

// Thread function to handle each client
void *handle_client(void *client_socket_ptr) {
  int client_communication_socket = *((int *)client_socket_ptr);
  char buffer[1024] = {0};       // To receive HTTP request
  char http_request[1024] = {0}; // To store HTTP request

  // Read the HTTP request from the client
  int bytes_read =
      read(client_communication_socket, buffer, sizeof(buffer) - 1);
  if (bytes_read < 0) {
    perror("SERVER: read failed");
    close(client_communication_socket);
    return NULL;
  }
  buffer[bytes_read] = '\0'; // Add null terminator

  // Save HTTP request to send later
  strcpy(http_request, buffer);

  // Extract the server address from the HTTP request
  char *ptr = strstr(buffer, "Host: ");
  if (ptr == NULL) {
    fprintf(stderr, "SERVER: Host header not found in HTTP request\n");
    close(client_communication_socket);
    return NULL;
  }
  ptr += strlen("Host: ");
  char *end_ptr = strchr(ptr, '\r');
  if (end_ptr == NULL) {
    fprintf(stderr, "SERVER: Invalid HTTP request format\n");
    close(client_communication_socket);
    return NULL;
  }
  *end_ptr = '\0';
  printf("SERVER: HTTP request received to %s\n", ptr);

  // Perform DNS resolution to get the IP address
  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0; // Allow any protocol
  hints.ai_flags = AI_ADDRCONFIG;
  int ret = getaddrinfo(ptr, "http", &hints, &result);
  if (ret != 0) {
    fprintf(stderr, "SERVER: getaddrinfo failed: %s\n", gai_strerror(ret));
    close(client_communication_socket);
    return NULL;
  }

  // Create a client socket to connect to the server
  int client_fd;
  if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("CLIENT: socket creation failed");
    close(client_communication_socket);
    return NULL;
  }
  // Copy the resolved IP address to the server_address structure
  struct sockaddr_in server_address;
  struct sockaddr_in *resolved_addr = (struct sockaddr_in *)result->ai_addr;
  memcpy(&server_address.sin_addr, &resolved_addr->sin_addr,
         sizeof(struct in_addr));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(80);
  freeaddrinfo(result);

  // Connect to the server
  if (connect(client_fd, (struct sockaddr *)&server_address,
              sizeof(server_address)) < 0) {
    perror("CLIENT: connection failed");
    close(client_communication_socket);
    close(client_fd);
    return NULL;
  }

  // Send the HTTP request to the server
  send(client_fd, http_request, strlen(http_request), 0);
  printf("CLIENT: HTTP request sent to %s\n", ptr);

  // Read the response from the server and send it back to the client
  char response[1024] = {0};
  int bytes_received = 0;
  while ((bytes_received = read(client_fd, response, sizeof(response))) > 0) {
    int bytes_sent = send(client_communication_socket, response, bytes_received,
                          0); // Send to client
    if (bytes_sent < 0) {
      perror("SERVER: send failed");
      close(client_communication_socket);
      close(client_fd);
      return NULL;
    }
    printf("CLIENT: HTTP response received from %s. SERVER: HTTP response sent "
           "back to client\n",
           ptr);
    memset(response, 0, sizeof(response)); // Clear response buffer
  }
  if (bytes_received < 0) {
    perror("CLIENT: read failed");
    close(client_communication_socket);
    close(client_fd);
    return NULL;
  } else if (bytes_received == 0) {
    printf("CLIENT: Server closed connection\n");
  }

  // Close connections
  close(client_fd);
  close(client_communication_socket);
  return NULL;
}

// Close server socket on Ctrl-C
void handle_sigint(int sig) {
  if (sig == SIGINT) {
    printf("Signal received: %d, exiting...\n", sig);
    close(server_fd);
    exit(EXIT_SUCCESS);
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <port number> (i.e. 8080)\n", argv[0]);
    return 1;
  }

  // Parse port number
  int port = atoi(argv[1]);

  // close server socket on Ctrl-C
  signal(SIGINT, handle_sigint);

  // Create server socket, bind, and listen for connection
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("SERVER: socket failed");
    exit(EXIT_FAILURE);
  }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("SERVER: bind failed");
    exit(EXIT_FAILURE);
  }
  if (listen(server_fd, 3) < 0) {
    perror("SERVER: listen failed");
    exit(EXIT_FAILURE);
  }

  // Print server information
  printf("SERVER: started succesfully. Listening on port %d\n\n", port);
  printf(
      "Please configure your browser to use http proxy on 127.0.0.1:%d\n"
      "Open a web page using http protocol only (i.e http://example.com/)\n\n",
      port);

  // Wait for connections
  while (1) {
    int client_communication_socket;
    if ((client_communication_socket =
             accept(server_fd, (struct sockaddr *)&address,
                    (socklen_t *)&addrlen)) < 0) {
      perror("SERVER: accept failed");
      exit(EXIT_FAILURE);
    }

    // Create a new thread to handle each client connection
    pthread_t thread;
    if (pthread_create(&thread, NULL, handle_client,
                       &client_communication_socket) != 0) {
      perror("SERVER: pthread_create failed");
      close(client_communication_socket);
    } else {
      // Store thread in the array
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_threads[i] == 0) {
          client_threads[i] = thread;
          break;
        }
      }
    }
  }

  close(server_fd);
  return 0;
}

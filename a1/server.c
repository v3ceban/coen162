#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
int server_fd, client_communication_socket;

// Signal handler for Ctrl+C
void sigintHandler(int sig_num) {
  printf("\nCtrl+C received. Closing connections and exiting.\n");
  close(server_fd);
  close(client_communication_socket);
  exit(EXIT_SUCCESS);
}

int main() {
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  char buffer[1024] = {0};

  // Register signal handler for Ctrl+C (only needed for testing purposes)
  signal(SIGINT, sigintHandler);

  // Create server socket, bind, and listen for connection
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 3) < 0) {
    perror("listen failed");
    exit(EXIT_FAILURE);
  }
  printf("Server listening on port %d\n", PORT);

  // Wait for connections indefinitely
  while (1) {
    if ((client_communication_socket =
             accept(server_fd, (struct sockaddr *)&address,
                    (socklen_t *)&addrlen)) < 0) {
      perror("accept failed");
      exit(EXIT_FAILURE);
    }

    // Read the HTTP request from the client
    read(client_communication_socket, buffer, 1024);
    printf("HTTP request received: %s\n", buffer);

    // Save HTTP request
    char http_request[1024] = {0};
    strcpy(http_request, buffer);

    // Extract the server address from the HTTP request
    char *ptr = strstr(buffer, "Host: ");
    if (ptr == NULL) {
      fprintf(stderr, "Host header not found in HTTP request\n");
      close(client_communication_socket);
      continue;
    }
    ptr += strlen("Host: ");
    char *end_ptr = strchr(ptr, '\r');
    if (end_ptr == NULL) {
      fprintf(stderr, "Invalid HTTP request format\n");
      close(client_communication_socket);
      continue;
    }
    *end_ptr = '\0'; // Null-terminate the server address

    // Perform DNS resolution to get the IP address
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0; // Set protocol to 0 to allow any protocol
    hints.ai_flags =
        AI_ADDRCONFIG; // Add this line to improve address configuration

    int ret = getaddrinfo(ptr, "http", &hints, &result);
    if (ret != 0) {
      fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(ret));
      close(client_communication_socket);
      continue;
    }

    // Create a socket to connect to the server
    int client_fd;
    struct sockaddr_in server_address;
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      perror("socket creation failed");
      close(client_communication_socket);
      continue;
    }

    // Copy the resolved IP address to the server_address structure
    struct sockaddr_in *resolved_addr = (struct sockaddr_in *)result->ai_addr;
    memcpy(&server_address.sin_addr, &resolved_addr->sin_addr,
           sizeof(struct in_addr));
    server_address.sin_family =
        AF_INET; // Add this line to explicitly set the address family
    server_address.sin_port = htons(80); // Set the port to 80 for HTTP
    freeaddrinfo(result);

    printf("Connecting to server at IP: %s\n",
           inet_ntoa(server_address.sin_addr));
    // Connect to the server
    if (connect(client_fd, (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {
      perror("connection failed");
      close(client_communication_socket);
      close(client_fd);
      continue;
    }
    printf("Connected\n");

    // Send the HTTP request to the server
    send(client_fd, http_request, strlen(http_request), 0);
    printf("HTTP request sent\n");

    // Read the response from the server
    char response[1024] = {0};
    int bytes_received = 0;
    while ((bytes_received = read(client_fd, response, 1024)) > 0) {
      // Send the response back to the client
      send(client_communication_socket, response, bytes_received, 0);
      printf("HTTP response sent\n");
      memset(response, 0, sizeof(response)); // Clear response buffer
    }

    if (bytes_received < 0) {
      perror("read failed");
    }
    // Close connections
    close(client_fd);
    close(client_communication_socket);
  }

  return 0;
}

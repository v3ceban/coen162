#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 20
#define CACHE_DIR "./cache/"
#define CACHE_CONTROL_HEADER "If-Modified-Since: "

int server_fd;
pthread_t client_threads[MAX_CLIENTS];
char last_modified_time[128] = {0};

// Function to get current time in HTTP date format
void get_http_time(char *buffer, size_t buffer_size) {
  time_t now = time(NULL);
  struct tm tm = *gmtime(&now);
  strftime(buffer, buffer_size, "%a, %d %b %Y %H:%M:%S %Z", &tm);
}

// Function to check if the cache directory exists, if not, create it
void init_cache_dir() {
  struct stat st = {0};
  if (stat(CACHE_DIR, &st) == -1) {
    mkdir(CACHE_DIR, 0700);
  }
}

// Function to generate a cache file path based on the URL
void get_cache_file_path(const char *url, char *cache_file_path,
                         size_t buffer_size) {
  snprintf(cache_file_path, buffer_size, "%s%s", CACHE_DIR, url);
  for (char *p = cache_file_path + strlen(CACHE_DIR); *p; p++) {
    if (*p == '/' || *p == ':')
      *p = '_';
  }
}

// Thread function to handle each client
void *handle_client(void *client_socket_ptr) {
  int client_communication_socket = *((int *)client_socket_ptr);
  char buffer[1024] = {0};
  char http_request[1024] = {0};
  char url[256] = {0};

  int bytes_read =
      read(client_communication_socket, buffer, sizeof(buffer) - 1);
  if (bytes_read < 0) {
    perror("Error: read failed");
    close(client_communication_socket);
    return NULL;
  }
  buffer[bytes_read] = '\0';
  strcpy(http_request, buffer);

  char *host_ptr = strstr(buffer, "Host: ");
  if (host_ptr == NULL) {
    close(client_communication_socket);
    return NULL;
  }
  host_ptr += strlen("Host: ");
  char *host_end_ptr = strchr(host_ptr, '\r');
  if (host_end_ptr == NULL) {
    close(client_communication_socket);
    return NULL;
  }
  *host_end_ptr = '\0';

  char *url_ptr = strstr(buffer, "GET ");
  if (url_ptr == NULL) {
    close(client_communication_socket);
    return NULL;
  }
  url_ptr += strlen("GET ");
  char *url_end_ptr = strchr(url_ptr, ' ');
  if (url_end_ptr == NULL) {
    close(client_communication_socket);
    return NULL;
  }
  *url_end_ptr = '\0';
  snprintf(url, sizeof(url), "%s%s", host_ptr, url_ptr);

  char cache_file_path[512];
  get_cache_file_path(url, cache_file_path, sizeof(cache_file_path));

  FILE *cache_file = fopen(cache_file_path, "r");
  if (cache_file != NULL) {
    fgets(last_modified_time, sizeof(last_modified_time), cache_file);
    fseek(cache_file, strlen(last_modified_time), SEEK_SET);

    char if_modified_since_header[256];
    snprintf(if_modified_since_header, sizeof(if_modified_since_header),
             "%s%s\r\n", CACHE_CONTROL_HEADER, last_modified_time);

    char *header_end_ptr = strstr(http_request, "\r\n\r\n");
    if (header_end_ptr != NULL) {
      *header_end_ptr = '\0';
      strcat(http_request, if_modified_since_header);
      strcat(http_request, "\r\n\r\n");
    }
  }

  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;
  int ret = getaddrinfo(host_ptr, "http", &hints, &result);
  if (ret != 0) {
    fprintf(stderr, "Error: getaddrinfo failed: %s\n", gai_strerror(ret));
    close(client_communication_socket);
    if (cache_file != NULL)
      fclose(cache_file);
    return NULL;
  }

  int client_fd;
  if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Error: socket creation failed");
    close(client_communication_socket);
    if (cache_file != NULL)
      fclose(cache_file);
    return NULL;
  }

  struct sockaddr_in server_address;
  struct sockaddr_in *resolved_addr = (struct sockaddr_in *)result->ai_addr;
  memcpy(&server_address.sin_addr, &resolved_addr->sin_addr,
         sizeof(struct in_addr));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(80);
  freeaddrinfo(result);

  if (connect(client_fd, (struct sockaddr *)&server_address,
              sizeof(server_address)) < 0) {
    perror("CLIENT: connection failed");
    close(client_communication_socket);
    close(client_fd);
    if (cache_file != NULL)
      fclose(cache_file);
    return NULL;
  }

  send(client_fd, http_request, strlen(http_request), 0);

  char response[1024] = {0};
  int bytes_received = 0;
  int is_not_modified = 0;

  while ((bytes_received = read(client_fd, response, sizeof(response))) > 0) {
    if (cache_file != NULL &&
        strstr(response, "HTTP/1.1 304 Not Modified") != NULL) {
      is_not_modified = 1;
      break;
    }
    int bytes_sent =
        send(client_communication_socket, response, bytes_received, 0);
    if (bytes_sent < 0) {
      perror("SERVER: send failed");
      close(client_communication_socket);
      close(client_fd);
      if (cache_file != NULL)
        fclose(cache_file);
      return NULL;
    }
    if (cache_file == NULL) {
      cache_file = fopen(cache_file_path, "w");
      if (cache_file != NULL) {
        char http_time[128];
        get_http_time(http_time, sizeof(http_time));
        fprintf(cache_file, "%s\n", http_time);
      }
    }
    if (cache_file != NULL)
      fwrite(response, 1, bytes_received, cache_file);
    memset(response, 0, sizeof(response));
  }

  if (is_not_modified && cache_file != NULL) {
    printf("Cache hit\n");
    fseek(cache_file, strlen(last_modified_time) + 1, SEEK_SET);
    while ((bytes_received = fread(response, 1, sizeof(response), cache_file)) >
           0) {
      send(client_communication_socket, response, bytes_received, 0);
      memset(response, 0, sizeof(response));
    }
  }

  close(client_communication_socket);
  close(client_fd);
  if (cache_file != NULL)
    fclose(cache_file);
  return NULL;
}

void handle_signal(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    close(server_fd);
    exit(0);
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <port number> (i.e. 8080)\n", argv[0]);
    return 1;
  }

  int port = atoi(argv[1]);

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  init_cache_dir();

  struct sockaddr_in address;
  int addrlen = sizeof(address);
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Error: socket failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Error: bind failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }
  if (listen(server_fd, 3) < 0) {
    perror("Error: listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Server started succesfully. Listening on port %d\n\n", port);
  printf(
      "Please configure your browser to use http proxy on 127.0.0.1:%d\nOpen a "
      "web page using http protocol only (i.e http://neverssl.com/)\n\n",
      port);

  while (1) {
    int client_communication_socket;
    if ((client_communication_socket =
             accept(server_fd, (struct sockaddr *)&address,
                    (socklen_t *)&addrlen)) < 0) {
      perror("Error: accept failed");
      close(server_fd);
      exit(EXIT_FAILURE);
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, handle_client,
                       &client_communication_socket) != 0) {
      perror("Error: pthread_create failed");
      close(client_communication_socket);
    } else {
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

#include "cache.h"
#include "helpers.h"
#include "sbuf.h"
#include <stdio.h>
#include <strings.h>
#include <time.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define SBUFSIZE 32
/* Cache file name */
#define CACHE_FILE "cache"
#define TIMESTAMP "timestamp.txt"

const int NTHREADS = 8; // number of working threads
sbuf_t sbuf;            // shared buffer of connected descriptors
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

// Helper and thread functions
int handle_uri(char *uri, char *hostname, char *path, int *port);
void handle_proxy(int fd);
void *thread(void *vargp);
void read_timestamp_from_file(char *buffer, size_t size);
void save_timestamp_to_file(void);

int main(int argc, char **argv) {
  int i, listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char client_hostname[MAXLINE], client_port[MAXLINE];
  pthread_t tid;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);
  printf(">Server started listening port %s\n", argv[1]);
  sbuf_init(&sbuf, SBUFSIZE);
  printf(">Shared buffer initialized\n");

  cache_init(CACHE_FILE);
  printf(">Cache initialized\n");

  save_timestamp_to_file();
  printf(">Timestamp saved\n");

  for (i = 0; i < NTHREADS; i++) { /* Create worker threads */
    int *id = Malloc(sizeof(int));
    *id = i;
    Pthread_create(&tid, NULL, thread, (void *)id);
  }
  printf(">Worker threads created\n");

  Signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE

  while (1) {
    clientlen = sizeof(struct sockaddr_storage); /* Important! */
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE,
                client_port, MAXLINE, 0);
    sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
    printf(">cilentfd %d in queue \n", connfd);
    printf(">Accepted connection from (%s, %s)\n", client_hostname,
           client_port);
  }
  sbuf_deinit(&sbuf);
  cache_deinit();
  exit(0);
}

void *thread(void *vargp) {
  Pthread_detach(pthread_self());
  int id = *((int *)vargp);
  Free(vargp);
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    printf("Working thread[%d] >> handling cilentfd[%d]\n", id, connfd);
    handle_proxy(connfd);
    Close(connfd);
    printf("Working thread[%d] << cilentfd[%d] closed!\n", id, connfd);
  }
  return NULL;
}

void handle_proxy(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
  char server_buf[MAXLINE], obj[MAX_OBJECT_SIZE];
  char last_modified[128];
  rio_t rio_client, rio_server;
  int serverfd;
  int port_int;
  size_t n, obj_len = 0;

  rio_readinitb(&rio_client, fd);
  rio_readlineb(&rio_client, buf, MAXLINE);

  sscanf(buf, "%s %s %s", method, uri, version);
  printf("Request line:\n");
  printf("%s", buf);
  if (strcasecmp(method, "GET") != 0) {
    printf("501: Proxy does not implement this method\n");
    return;
  }
  if (!handle_uri(uri, hostname, path, &port_int)) {
    printf("400: Proxy could not parse the request\n");
    return;
  } else {
    // connect to server
    printf("hostname: %s, url: %s, port: %d\n", hostname, path, port_int);
    sprintf(port, "%d", port_int);

    read_timestamp_from_file(last_modified, sizeof(last_modified));

    // read response from server
    cache_block *block = cache_find(hostname, path, port_int);
    if (block != NULL) {
      printf("Cache hit!\n");

      // Send an If-Modified-Since GET request to the server
      serverfd = open_clientfd(hostname, port);
      if (serverfd < 0) {
        printf("404: Proxy could not connect to this server\n");
        return;
      }

      // send request to server
      rio_readinitb(&rio_server, serverfd);
      sprintf(server_buf, "GET %s HTTP/1.0\r\n", path);
      rio_writen(serverfd, server_buf, strlen(server_buf));
      sprintf(server_buf, "Host: %s\r\n", hostname);
      rio_writen(serverfd, server_buf, strlen(server_buf));
      if (strlen(last_modified) > 0) {
        sprintf(server_buf, "If-Modified-Since: %s\r\n", last_modified);
        rio_writen(serverfd, server_buf, strlen(server_buf));
      }
      rio_writen(serverfd, (void *)user_agent_hdr, strlen(user_agent_hdr));
      rio_writen(serverfd, (void *)connection_hdr, strlen(connection_hdr));
      rio_writen(serverfd, (void *)proxy_connection_hdr,
                 strlen(proxy_connection_hdr));
      rio_writen(serverfd, "\r\n", 2);

      // read response header
      rio_readlineb(&rio_server, buf, MAXLINE);
      sscanf(buf, "%s %s", method, version);
      if (strcmp(method, "HTTP/1.0") == 0 || strcmp(method, "HTTP/1.1") == 0) {
        if (strcmp(version, "304") == 0) {
          // Not modified
          printf("Server response: Not Modified\n");
          rio_writen(fd, block->content, block->size);
          printf("Respond %ld bytes object from cache:\n", block->size);
          Close(serverfd);
          return;
        }
      }

      // read the rest of the response from server
      printf("Server response: Modified\n");
      obj_len = 0;
      while ((n = rio_readnb(&rio_server, server_buf, MAXLINE)) > 0) {
        obj_len += n;
        if (obj_len <= MAX_OBJECT_SIZE) {
          memcpy(obj + obj_len - n, server_buf, n);
        }
        rio_writen(fd, server_buf, n); // send response to client
      }
      if (obj_len <= MAX_OBJECT_SIZE) {
        cache_insert(hostname, path, port_int, obj, obj_len, CACHE_FILE);
        printf("Cache updated %ld bytes object:\n", obj_len);
      } else {
        printf("Cache failed, object over limit size!\n");
      }
      printf("Respond %ld bytes object:\n", obj_len);
      Close(serverfd);
      return;
    } else {
      printf("Cache miss!\n");
      serverfd = open_clientfd(hostname, port);
      if (serverfd < 0) {
        printf("404: Proxy could not connect to this server\n");
        return;
      }

      // send request to server
      rio_readinitb(&rio_server, serverfd);
      // send request header
      sprintf(server_buf, "GET %s HTTP/1.0\r\n", path);
      rio_writen(serverfd, server_buf, strlen(server_buf));
      sprintf(server_buf, "Host: %s\r\n", hostname);
      rio_writen(serverfd, server_buf, strlen(server_buf));
      rio_writen(serverfd, (void *)user_agent_hdr, strlen(user_agent_hdr));
      rio_writen(serverfd, (void *)connection_hdr, strlen(connection_hdr));
      rio_writen(serverfd, (void *)proxy_connection_hdr,
                 strlen(proxy_connection_hdr));
      rio_writen(serverfd, "\r\n", 2);

      // read response header and body
      while ((n = rio_readnb(&rio_server, server_buf, MAXLINE)) > 0) {
        obj_len += n;
        if (obj_len <= MAX_OBJECT_SIZE) {
          memcpy(obj + obj_len - n, server_buf, n);
        }
        rio_writen(fd, server_buf, n); // send response to client
      }
      if (obj_len <= MAX_OBJECT_SIZE) {
        cache_insert(hostname, path, port_int, obj, obj_len, CACHE_FILE);
        printf("Cache insert %ld bytes object:\n", obj_len);
      } else {
        printf("Cache failed, object over limit size!\n");
      }
      printf("Respond %ld bytes object:\n", obj_len);
      Close(serverfd);
    }
  }
}

void get_current_timestamp(char *buffer, size_t size) {
  time_t now = time(NULL);
  struct tm *tm_info = gmtime(&now);
  strftime(buffer, size, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
}

void save_timestamp_to_file() {
  char buffer[128];
  get_current_timestamp(buffer, sizeof(buffer));
  FILE *file = fopen(TIMESTAMP, "w");
  if (file) {
    fprintf(file, "%s", buffer);
    fclose(file);
  }
}

void read_timestamp_from_file(char *buffer, size_t size) {
  FILE *file = fopen(TIMESTAMP, "r");
  if (file) {
    fgets(buffer, size, file);
    fclose(file);
  } else {
    // If the file does not exist, set an empty string
    buffer[0] = '\0';
  }
}

// handle the uri that user sends
int handle_uri(char *uri, char *hostname, char *path, int *port) {
  const char *temp_head, *temp_tail;
  char scheme[10], port_str[10];
  size_t temp_len;

  temp_head = uri;

  // get scheme
  temp_tail = strstr(temp_head, "://");
  if (temp_tail == NULL) {
    return 0;
  }
  temp_len = temp_tail - temp_head;
  strncpy(scheme, temp_head, temp_len);
  scheme[temp_len] = '\0';
  if (strcasecmp(scheme, "http") != 0) {
    return 0;
  }

  // get hostname
  temp_head = temp_tail + 3;
  temp_tail = temp_head;
  while (*temp_tail != '\0') {
    if (*temp_tail == ':' || *temp_tail == '/') {
      break;
    }
    temp_tail++;
  }
  temp_len = temp_tail - temp_head;
  strncpy(hostname, temp_head, temp_len);
  hostname[temp_len] = '\0';

  // get port
  temp_head = temp_tail;
  if (*temp_head == ':') {
    temp_head++;
    temp_tail = temp_head;
    while (*temp_tail != '\0') {
      if (*temp_tail == '/') {
        break;
      }
      temp_tail++;
    }
    temp_len = temp_tail - temp_head;
    strncpy(port_str, temp_head, temp_len);
    port_str[temp_len] = '\0';
    *port = atoi(port_str);
  } else {
    *port = 80;
  }

  // get path
  temp_head = temp_tail;
  if (*temp_head == '/') {
    temp_tail = temp_head;
    while (*temp_tail != '\0') {
      temp_tail++;
    }
    temp_len = temp_tail - temp_head;
    strncpy(path, temp_head, temp_len);
    path[temp_len] = '\0';
    return 1;
  } else {
    path[0] = '/';
    path[1] = '\0';
    return 1;
  }
}

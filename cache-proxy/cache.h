#include "helpers.h"
#include <stdlib.h>

typedef struct cache_block {
  int freq; // frequency of access
  int port;
  char hostname[MAXLINE];
  char path[MAXLINE];
  char *content;            // the content of the cache block (the response)
  size_t size;              // the size of the content
  struct cache_block *prev; // the prev cache block
  struct cache_block *next; // the next cache block
  pthread_rwlock_t block_lock;
} cache_block;

typedef struct cache {         // the cache is a double linked list
  struct cache_block *head;    // the head of the cache
  struct cache_block *tail;    // the tail of the cache
  size_t c_size;               // the total size of the cache
  pthread_rwlock_t cache_lock; // the lock of the cache
} Cache;

void cache_init(const char *filename); // initialize the cache
void cache_deinit(void);               // free the cache
void print_cache(void);                // for debugging

void cache_insert(char *hostname, char *path, int port, char *content,
                  size_t size, const char *filename);
cache_block *cache_find(char *hostname, char *path, int port);
void cache_delete(void);
void cache_save(const char *filename);
void cache_retreive(const char *filename);

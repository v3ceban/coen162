#include "cache.h"
#include "helpers.h"
#include <stdio.h>
#include <string.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static Cache *cache;

void cache_init(const char *filename) {
  cache = Malloc(sizeof(Cache));
  cache->head = Malloc(sizeof(cache_block));
  cache->tail = Malloc(sizeof(cache_block));
  cache->head->next = cache->tail;
  cache->tail->prev = cache->head;
  cache->head->prev = NULL;
  cache->tail->next = NULL;
  cache->c_size = 0;
  pthread_rwlock_init(&cache->cache_lock, NULL);
  cache_retreive(filename);
}

void cache_deinit(void) {
  cache_block *temp = cache->head;
  while (temp != NULL) {
    cache->head = temp->next;
    if (temp->content != NULL) {
      Free(temp->content);
    }
    pthread_rwlock_destroy(&temp->block_lock);
    Free(temp);
    temp = cache->head;
  }
  pthread_rwlock_destroy(&cache->cache_lock);
  Free(cache);
}

cache_block *cache_find(char *hostname, char *path, int port) {
  pthread_rwlock_rdlock(&cache->cache_lock);
  cache_block *temp = cache->head->next;

  while (temp != cache->tail) {
    if (strcmp(temp->hostname, hostname) == 0 &&
        strcmp(temp->path, path) == 0 && temp->port == port) {
      // move the block to the head
      pthread_rwlock_unlock(&cache->cache_lock);
      pthread_rwlock_wrlock(&cache->cache_lock);

      pthread_rwlock_wrlock(&temp->block_lock);
      temp->freq = temp->freq + 1;
      temp->prev->next = temp->next;
      temp->next->prev = temp->prev;
      temp->next = cache->head->next;
      temp->prev = cache->head;
      pthread_rwlock_unlock(&temp->block_lock);

      cache->head->next->prev = temp;
      cache->head->next = temp;

      pthread_rwlock_unlock(&cache->cache_lock);
      return temp;
    }
    temp = temp->next;
  }
  pthread_rwlock_unlock(&cache->cache_lock);
  return NULL;
}

void cache_insert(char *hostname, char *path, int port, char *content,
                  size_t size, const char *last_modified,
                  const char *filename) {
  pthread_rwlock_wrlock(&cache->cache_lock);
  cache_block *temp = Malloc(sizeof(cache_block));
  pthread_rwlock_init(&temp->block_lock, NULL);
  strcpy(temp->hostname, hostname);
  strcpy(temp->path, path);
  temp->port = port;
  temp->content = Malloc(size);
  memcpy(temp->content, content, size);
  temp->size = size;
  temp->freq = 0;
  strcpy(temp->last_modified, last_modified);
  temp->next = cache->head->next;
  temp->prev = cache->head;
  // insert the block to the head
  cache->head->next->prev = temp;
  cache->head->next = temp;
  cache->c_size += size;
  // delete the last block if the cache is full
  while (cache->c_size > MAX_CACHE_SIZE) {
    cache_delete();
  }
  pthread_rwlock_unlock(&cache->cache_lock);
  cache_save(filename);
}

void cache_delete(void) // delete the last block
{
  pthread_rwlock_wrlock(&cache->cache_lock);
  cache_block *temp = cache->tail->prev;
  if (temp == cache->head) {
    pthread_rwlock_unlock(&cache->cache_lock);
    return;
  }
  temp->prev->next = cache->tail;
  cache->tail->prev = temp->prev;
  cache->c_size -= temp->size;
  if (temp->content != NULL) {
    Free(temp->content);
  }
  pthread_rwlock_destroy(&temp->block_lock);
  Free(temp);
  pthread_rwlock_unlock(&cache->cache_lock);
}

void print_cache(void) {
  cache_block *temp = cache->head->next;
  while (temp != cache->tail) {
    printf("! hostname: %s, path: %s, port: %d, size: %ld, freq: %d\n",
           temp->hostname, temp->path, temp->port, temp->size, temp->freq);
    temp = temp->next;
  }
}

void cache_save(const char *filename) {
  FILE *file = fopen(filename, "ab");
  if (file == NULL) {
    return;
  }

  cache_block *temp = cache->head->next;
  while (temp != cache->tail) {
    fwrite(temp, sizeof(cache_block), 1, file);
    fwrite(temp->content, temp->size, 1, file);
    fwrite(temp->last_modified, MAXLINE, 1, file);
    temp = temp->next;
  }

  fclose(file);
}

void cache_retreive(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    return;
  }
  cache_block temp;
  while (fread(&temp, sizeof(cache_block), 1, file) == 1) {
    temp.content = malloc(temp.size); // Allocate memory for content
    if (temp.content == NULL) {
      // Handle allocation failure
      fclose(file);
      return;
    }
    fread(temp.content, temp.size, 1, file); // Read content from file
    cache_insert(temp.hostname, temp.path, temp.port, temp.content, temp.size,
                 temp.last_modified, filename);
  }
  fclose(file);
}

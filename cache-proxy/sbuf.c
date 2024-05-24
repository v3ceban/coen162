#include "sbuf.h"
#include "helpers.h"

/* Create an empty, bounded, shared FIFO buffer with n slots */
void sbuf_init(sbuf_t *sp, int n) {
  sp->buf = Calloc(n, sizeof(int));
  sp->n = n; /* Buffer holds max of n items */
  sp->slot_num = n;
  sp->item_num = 0;
  sp->front = sp->rear = 0; /* Empty buffer iff front == rear */
  pthread_mutex_init(&sp->mutex, NULL);
  pthread_cond_init(&sp->slots, NULL);
  pthread_cond_init(&sp->items, NULL);
}

/* Clean up buffer sp */
void sbuf_deinit(sbuf_t *sp) {
  Free(sp->buf);
  pthread_mutex_destroy(&sp->mutex);
  pthread_cond_destroy(&sp->slots);
  pthread_cond_destroy(&sp->items);
}

/* Insert item onto the rear of shared buffer sp */
void sbuf_insert(sbuf_t *sp, int item) { // slot - 1; item + 1
  pthread_mutex_lock(&sp->mutex);
  while (sp->slot_num == 0) { // wait until sp->slot_num - 1 >= 0;
    pthread_cond_wait(&sp->slots, &sp->mutex);
  }
  sp->buf[(++sp->rear) % (sp->n)] = item; /* Insert the item */
  sp->slot_num--;
  sp->item_num++;
  pthread_cond_broadcast(&sp->items); // wake up
  pthread_mutex_unlock(&sp->mutex);
}

/* Remove and return the first item from buffer sp */
int sbuf_remove(sbuf_t *sp) { // slot + 1; item - 1
  int item;
  pthread_mutex_lock(&sp->mutex);
  while (sp->item_num == 0) { // wait until sp->item_num - 1 >= 0;
    pthread_cond_wait(&sp->items, &sp->mutex);
  }
  item = sp->buf[(++sp->front) % (sp->n)]; /* Remove the item */
  sp->slot_num++;
  sp->item_num--;
  pthread_cond_broadcast(&sp->slots); // wake up
  pthread_mutex_unlock(&sp->mutex);
  return item;
}

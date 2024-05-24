#include "helpers.h"

typedef struct {
  int *buf;     /* Buffer array */
  int n;        /* Maximum number of slots */
  int slot_num; /*free slots nnumber*/
  int item_num; /*items number*/
  int front;    /* buf[(front+1)%n] is first item */
  int rear;     /* buf[rear%n] is last item */
  pthread_mutex_t mutex;
  pthread_cond_t slots;
  pthread_cond_t items;
} sbuf_t;

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

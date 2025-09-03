#ifndef LIST_H
#define LIST_H

typedef struct list_node_t {
  struct list_node_t *prev;
  struct list_node_t *next;
   void *val;
} list_node_t;

typedef struct list_t {
  struct list_node_t *head;
  struct list_node_t *tail;
  int len;
  void (*free) (void *ptr);
} list_t;

list_t *listCreate(void);
void listEmpty(list_t *list);
list_t *listAddNodeToHead(list_t *list, void *value);
list_t *listAddNodeToTail(list_t *list, void *value);
list_t *listInsertNode(list_t *list, list_node_t *node, void *value, int after);
void listDeleteNode(list_t *list, list_node_t *node);
void listLinkNodeToHead(list_t *list, list_node_t *node);
list_node_t *listFindNode(list_t *list, list_node_t *node, void *value);

#endif

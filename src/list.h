#ifndef SRC_LIST_H_
#define SRC_LIST_H_

typedef struct list {
	struct list *prev;
	struct list *next;
} list;

/*
 * non-intrusive linked list
 */
typedef struct nlist_node {
	struct nlist_node *prev;
	struct nlist_node *next;
	void *n;
} nlist_node;

typedef struct nlist_head {
	nlist_node *head;
	nlist_node *tail;
} nlist_head;

static inline int nlist_empty(nlist_head *head) {
	return !head->head;
}
void nlist_push(nlist_node *t, nlist_head *head);
void nlist_remove(nlist_node *n, nlist_head *head);

#define NLIST_INIT() (nlist_head){NULL, NULL}

#define nlist_for_each(t, h) \
	for (nlist_node *__n = (h)->head; __n&&(t=(typeof(*t) *)__n->n); __n=__n->next)

#define nlist_for_each_n(t, node, h) \
	for (nlist_node *node = (h)->head; node&&(t=(typeof(*t) *)node->n); node=node->next)

#define INIT_LIST(list) {&list, &list}

#define OFFSET(type, member) (int)&((type *)0)->member
#define CONTAINER_OF(type, member, mp) (type *)((char *)(mp) - OFFSET(type, member))
#define LIST_TAIL(type, member, head)\
	({ \
		list * l = list_tail(head);\
		CONTAINER_OF(type, member, l);})

#define list_for_each(t, link, head) \
	for (t = CONTAINER_OF(typeof(*t), link, (head)->next); &t->link != (head);\
		t=CONTAINER_OF(typeof(*t), link, t->link.next))

static inline int list_empty(list *head) {
	return head == head->prev && head->prev == head->next;
}

static inline void list_remove(list *node) {
	node->prev->next = node->next;
	node->next->prev = node->prev;
}

list *list_take(list *head);

void list_push(list *t, list *head);

#endif /* SRC_LIST_H_ */

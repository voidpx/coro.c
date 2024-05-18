#include "list.h"

list *list_tail(list *head) {
	list *t = head->next;
	head->next->next->prev = head;
	head->next = head->next->next;
	t->prev = t->next = t;
	return t;
}

void list_push(list *t, list *head) {
	t->prev = head->prev;
	t->next = head;
	head->prev->next = t;
	head->prev = t;
}

void nlist_push(nlist_node *n, nlist_head *head) {
	if (!head->head) {
		head->head = head->tail = n;
	} else {
		n->prev = head->tail;
		head->tail->next = n;
		head->tail = n;
	}
}

void nlist_remove(nlist_node *n, nlist_head *head) {
	if (n == head->head) {
		head->head = n->next;
	}
	if (n == head->tail) {
		head->tail = n->prev;
	}
}

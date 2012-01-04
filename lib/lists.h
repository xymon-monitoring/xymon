#ifndef __LISTS_H__
#define __LISTS_H__

typedef struct listitem_t {
	void *data;
	void *keeper;
	struct listitem_t *next, *previous;
} listitem_t;

typedef struct listhead_t {
	char *listname;
	int len;
	struct listitem_t *head, *tail;
} listhead_t;


extern listhead_t *list_create(char *name);
extern void list_item_move(listhead_t *tolist, listitem_t *rec, char *info);
extern listitem_t *list_item_create(listhead_t *listhead, void *data, char *info);
extern void *list_item_delete(listitem_t *rec, char *info);
#endif


#include "util/arraylist.h"

#include <stdlib.h>
#include <string.h>

struct arraylist arraylist_create(void)
{
	return (struct arraylist){
		.data = malloc(sizeof(void *)),
		.data_sizes = malloc(sizeof(size_t)),
		.size = 0,
		.cap = 1,
	};
}

void arraylist_destroy(struct arraylist *al)
{
	for (size_t i = 0; i < al->size; ++i)
		free(al->data[i]);
	
	free(al->data);
	free(al->data_sizes);
}

void arraylist_add(struct arraylist *al, void const *new, size_t size)
{
	if (al->size >= al->cap) {
		al->cap *= 2;
		al->data = realloc(al->data, sizeof(void *) * al->cap);
		al->data_sizes = realloc(al->data_sizes, sizeof(void *) * al->cap);
	}

	al->data[al->size] = malloc(size);
	al->data_sizes[al->size] = size;
	memcpy(al->data[al->size], new, size);
	++al->size;
}

void arraylist_rm(struct arraylist *al, size_t ind)
{
	free(al->data[ind]);
	size_t mv_size = (al->size-- - ind) * sizeof(void *);
	memmove(al->data + ind, al->data + ind + 1, mv_size);
}

struct arraylist arraylist_copy(struct arraylist const *al)
{
	struct arraylist cp = arraylist_create();

	for (size_t i = 0; i < al->size; ++i)
		arraylist_add(&cp, al->data[i], al->data_sizes[i]);

	return cp;
}

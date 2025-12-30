#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#include "util.h"

#define UTIL_MAX_STR 1000000

char *program_name = "quickpick";
bool debug_mode = true;

Vector2 normalize_v2(Vector2 v)
{
    float d = sqrtf(v.x*v.x + v.y*v.y);
    return (Vector2) { v.x / d, v.y / d };
}

Vector2 add_v2(Vector2 v, Vector2 w)
{
    return (Vector2) { v.x + w.x, v.y + w.y };
}

Vector2 mult_cv2(float c, Vector2 v)
{
    return (Vector2) { c*v.x, c*v.y };
}

void assertf(bool value, const char *fmt, ...)
{
	if (!value) {
		if (fmt) {
			va_list args;
			va_start(args, fmt);
			vfprintf(stderr, fmt, args);
			va_end(args);
		} else {
			fprintf(stderr, "Programmer error, have to stop.\n");
		}
		exit(1);
	}
}

void debug(const char *fmt, ...)
{
	if (debug_mode) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
	}
}

void errexit(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(1);
}
/************************************** UTF-8 decoding ********************************************/

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#define UTIL_UTF8_ACCEPT 0
#define UTIL_UTF8_REJECT 12

static const uint8_t utf8d[] = {
  // The first part of the table maps bytes to character classes that
  // to reduce the size of the transition table and create bitmasks.
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

  // The second part is a transition table that maps a combination
  // of a state of the automaton and a character class to a state.
   0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
  12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
  12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
  12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
  12,36,12,12,12,12,12,12,12,12,12,12,
};

static inline uint32_t utf8_decoder(uint32_t* state, uint32_t* codep, uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != UTIL_UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}

u32 *decode_string(const char *s, u64 *out_len)
{
	u64 len = strnlen(s, UTIL_MAX_STR);
	u32 *res = malloc((len+1)*sizeof(u32));
	if (!res)
		return NULL;
	u64 out_i = 0;
	u32 state = UTIL_UTF8_ACCEPT, codep = 0;
	for (u64 char_i=0; char_i<len; char_i++) {
		utf8_decoder(&state, &codep, (u8) s[char_i]);
		if (state == UTIL_UTF8_ACCEPT)
			res[out_i++] = codep;
		else if (state == UTIL_UTF8_REJECT) {
			free(res);
			return NULL;
		}
	}
	if (state != UTIL_UTF8_ACCEPT) {
	    free(res);
	    return NULL;
	}
	res[out_i] = 0;
	*out_len = out_i;
	return res;
}

/************************************** Dynarray **************************************************/

Dynarray *new_dynarray(u64 item_size)
{
	Dynarray *res = (Dynarray *) malloc(sizeof(Dynarray));
	res->length = 0;
	res->item_size = item_size;
	res->capacity = 8;
	res->d = (u8 *) calloc(item_size, res->capacity);
	return res;
}

void dynarray_add(void *dynarray, void *item)
{
	Dynarray *arr = (Dynarray *) dynarray;
	if ((arr->length + 1) > arr->capacity) {
		arr->capacity *= 2;
		u64 new_size = arr->capacity * arr->item_size;
		arr->d = realloc(arr->d, new_size);
		assertf(arr->d, "[%s ERROR] Failed to allocate new memory for dynamic array.\n", program_name);
	}
	memmove(arr->d + arr->item_size*arr->length, item, arr->item_size);
	arr->length += 1;
}

void dynarray_expand(void *dynarray, u64 new_capacity)
{
	Dynarray *arr = (Dynarray *) dynarray;
	bool need_expand = false;
	u64 new_size = 0;
	while (new_capacity > arr->capacity) {
		arr->capacity *= 2;
		u64 new_size = arr->capacity * arr->item_size;
		need_expand = true;
	}
	if (need_expand) {
		assertf(arr->capacity == new_size / arr->item_size,
			"[%s ERROR] integral overflow in dynamic array.\n", program_name);
		arr->d = realloc(arr->d, new_size);
		assertf(arr->d, "[%s ERROR] Failed to allocate new memory for dynamic array.\n");
	}
}

void *dynarray_get(void *dynarray, u64 i)
{
	Dynarray *arr = (Dynarray *) dynarray;
	assertf(i < arr->length, "[%s ERROR] out of range access in dynamic array.\n", program_name);
	return (void *) &arr->d[i * arr->item_size];
}

void dynarray_delete(void *dynarray, u64 i)
{
	Dynarray *arr = (Dynarray *) dynarray;
	assertf(i < arr->length, "[%s ERROR] out of range delete in dynamic array.\n", program_name);
	memmove(arr->d + i*arr->item_size, arr->d + (i+1)*arr->item_size, (arr->length-(i+1))*arr->item_size);
	arr->length--;
}

/************************************** Hash Table ************************************************/

u64 fnv1a_64(void *data, u64 len)
{
	u8 *p = data;
	u64 prime = 0x00000100000001b3;
	u64 hash = 0xcbf29ce484222325;
	for (size_t i=0; i<len; i++) {
		hash ^= p[i];
		hash *= prime;
	}
	return hash;
}

Hash_Table create_hash_table(u64 n_entries)
{
	Hash_Table res;
	res.d = (Hash_Entry *) malloc(n_entries*sizeof(Hash_Entry));
	memset(res.d, 0, n_entries * sizeof(Hash_Entry));
	res.n_entries = n_entries;
	return res;
}

void destroy_hash_table(Hash_Table *table)
{
	free(table->d);
}

bool hash_table_set(Hash_Table *table, void *key, u64 key_len, void *value)
{
	if (!key)
		return false;
	u64 n = table->n_entries;
	u64 h = fnv1a_64(key, key_len);
	u64 h_i_start = h % n;
	u64 h_i = h_i_start;
	Hash_Entry *e = &table->d[h_i];
	while (e->key) {
		h_i = (h_i + 1) % n;
		if (h_i == h_i_start) {
			return false;
		}
		e = &table->d[h_i];
	}
	e->hash = h;
	e->key = key;
	e->len = key_len;
	e->value = value;
	return true;
}

void *hash_table_get(Hash_Table *table, void *key, u64 key_len)
{
	if (!key) {
		return NULL;
	}
	u64 n = table->n_entries;
	u64 h = fnv1a_64(key, key_len);
	u64 h_i_start = h % n;
	u64 h_i = h_i_start;
	Hash_Entry *e = &table->d[h_i];
	while (e->hash != h) {
		if (e->key == NULL) {
			return NULL;
		}
		h_i = (h_i + 1) % n;
		if (h_i == h_i_start) {
			return NULL;
		}
		e = &table->d[h_i];
	}
	return e->value;
}

/**************************************************************************************************/

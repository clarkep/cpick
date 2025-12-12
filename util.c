#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#include "util.h"

char *program_name = "";
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
			fprintf(stderr, "Programming error, have to stop.\n");
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

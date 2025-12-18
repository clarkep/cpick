#ifndef QUICKPICK_UTIL_H
#define QUICKPICK_UTIL_H

#include <stdint.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.1415926535897932385
#endif
#define F_PI 3.1415926535897932385f

#define MIN(x, y) ((x)<(y) ? (x) : (y))
#define MAX(x, y) ((x)>(y) ? (x) : (y))
#define ABS(x) ((x)<0 ? -(x) : (x))
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct vector2 {
    float x;
    float y;
} Vector2;

typedef struct vector3 {
    float x;
    float y;
    float z;
} Vector3;

typedef struct vector4 {
    float x;
    float y;
    float z;
    float w;
} Vector4;

typedef struct hash_entry {
	u64 hash;
	void *key;
	u64 len;
	void *value;
} Hash_Entry;

typedef struct hash_table {
	Hash_Entry *d;
	u64 n_entries;
} Hash_Table;

Vector2 normalize_v2(Vector2 v);
Vector2 add_v2(Vector2 v, Vector2 w);
Vector2 mult_cv2(float c, Vector2 v);

void debug(const char *fmt, ...);

void assertf(bool value, const char *fmt, ...);

void errexit(char *fmt, ...);

u32 *decode_string(const char *s, u64 *out_len);

Hash_Table create_hash_table(u64 n_entries);

void destroy_hash_table(Hash_Table *table);

bool hash_table_set(Hash_Table *table, void *key, u64 key_len, void *value);

void *hash_table_get(Hash_Table *table, void *key, u64 key_len);

#endif
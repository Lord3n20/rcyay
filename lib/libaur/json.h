/*
 *  json.h - kleiner JSON-Parser (DOM) für libaur
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef AUR_JSON_H
#define AUR_JSON_H

#include <stddef.h>

typedef enum {
	AUR_JSON_NULL = 0,
	AUR_JSON_BOOL,
	AUR_JSON_NUMBER,
	AUR_JSON_STRING,
	AUR_JSON_ARRAY,
	AUR_JSON_OBJECT
} aur_json_type_t;

typedef struct aur_json_t aur_json_t;

struct aur_json_t {
	aur_json_type_t type;
	char *key;          /* Schlüssel, wenn Element eines Objekts */
	double number;      /* NUMBER, BOOL (0/1) */
	char *string;       /* STRING (UTF-8, ohne Escapes) */
	aur_json_t *child;  /* erstes Kind (ARRAY/OBJECT) */
	aur_json_t *next;   /* nächstes Geschwister */
	size_t count;       /* Anzahl Kinder */
};

/* Parst einen JSON-Text. Gibt NULL bei Fehler zurück; *err (falls nicht NULL)
 * zeigt dann auf eine statische Fehlermeldung. */
aur_json_t *aur_json_parse(const char *text, size_t len, const char **err);
void aur_json_free(aur_json_t *j);

/* Zugriff; alle Funktionen vertragen NULL. */
aur_json_t *aur_json_get(const aur_json_t *obj, const char *key);
aur_json_t *aur_json_index(const aur_json_t *arr, size_t i);
const char *aur_json_str(const aur_json_t *j);          /* NULL, wenn kein String */
double aur_json_num(const aur_json_t *j, double fallback);
const char *aur_json_get_str(const aur_json_t *obj, const char *key);
double aur_json_get_num(const aur_json_t *obj, const char *key, double fallback);

#endif

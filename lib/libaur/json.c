/*
 *  json.c - kleiner JSON-Parser (DOM) für libaur
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

#include "json.h"

#define AUR_JSON_MAXDEPTH 64

typedef struct {
	const char *p;
	const char *end;
	const char *err;
	int depth;
} parser_t;

static void skip_ws(parser_t *ps)
{
	while(ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' ||
				*ps->p == '\n' || *ps->p == '\r')) {
		ps->p++;
	}
}

static aur_json_t *new_node(aur_json_type_t type)
{
	aur_json_t *j = calloc(1, sizeof(*j));
	if(j) {
		j->type = type;
	}
	return j;
}

void aur_json_free(aur_json_t *j)
{
	while(j) {
		aur_json_t *next = j->next;
		aur_json_free(j->child);
		free(j->key);
		free(j->string);
		free(j);
		j = next;
	}
}

static int hexval(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int read_hex4(parser_t *ps, unsigned int *out)
{
	unsigned int v = 0;
	if(ps->end - ps->p < 4) {
		return -1;
	}
	for(int i = 0; i < 4; i++) {
		int h = hexval(ps->p[i]);
		if(h < 0) {
			return -1;
		}
		v = (v << 4) | (unsigned int)h;
	}
	ps->p += 4;
	*out = v;
	return 0;
}

static size_t put_utf8(char *o, unsigned int cp)
{
	if(cp < 0x80) {
		o[0] = (char)cp;
		return 1;
	} else if(cp < 0x800) {
		o[0] = (char)(0xC0 | (cp >> 6));
		o[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	} else if(cp < 0x10000) {
		o[0] = (char)(0xE0 | (cp >> 12));
		o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		o[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	o[0] = (char)(0xF0 | (cp >> 18));
	o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	o[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

/* ps->p zeigt auf das öffnende Anführungszeichen */
static char *parse_string(parser_t *ps)
{
	const char *start;
	char *out, *o;

	ps->p++;
	start = ps->p;
	/* Länge abschätzen: Ausgabe ist nie länger als die Eingabe */
	while(ps->p < ps->end && *ps->p != '"') {
		if(*ps->p == '\\') {
			ps->p++;
		}
		ps->p++;
	}
	if(ps->p >= ps->end) {
		ps->err = "unterminated string";
		return NULL;
	}
	out = o = malloc((size_t)(ps->p - start) + 1);
	if(!out) {
		ps->err = "out of memory";
		return NULL;
	}
	ps->p = start;
	while(*ps->p != '"') {
		unsigned char c = (unsigned char)*ps->p;
		if(c < 0x20) {
			ps->err = "control character in string";
			free(out);
			return NULL;
		}
		if(c != '\\') {
			*o++ = (char)c;
			ps->p++;
			continue;
		}
		ps->p++;
		switch(*ps->p++) {
			case '"': *o++ = '"'; break;
			case '\\': *o++ = '\\'; break;
			case '/': *o++ = '/'; break;
			case 'b': *o++ = '\b'; break;
			case 'f': *o++ = '\f'; break;
			case 'n': *o++ = '\n'; break;
			case 'r': *o++ = '\r'; break;
			case 't': *o++ = '\t'; break;
			case 'u': {
				unsigned int cp, lo;
				if(read_hex4(ps, &cp) != 0) {
					goto bad_escape;
				}
				if(cp >= 0xD800 && cp <= 0xDBFF) {
					if(ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
						ps->p += 2;
						if(read_hex4(ps, &lo) != 0 || lo < 0xDC00 || lo > 0xDFFF) {
							goto bad_escape;
						}
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					} else {
						cp = 0xFFFD;
					}
				} else if(cp >= 0xDC00 && cp <= 0xDFFF) {
					cp = 0xFFFD;
				}
				if(cp == 0) {
					/* NUL würde den C-String abschneiden */
					cp = 0xFFFD;
				}
				/* \uXXXX (6 Bytes) ergibt höchstens 3 Bytes, Paare (12) höchstens 4 */
				o += put_utf8(o, cp);
				break;
			}
			default:
				goto bad_escape;
		}
	}
	ps->p++;
	*o = '\0';
	return out;

bad_escape:
	ps->err = "invalid escape in string";
	free(out);
	return NULL;
}

static aur_json_t *parse_value(parser_t *ps);

static aur_json_t *parse_container(parser_t *ps, int is_object)
{
	aur_json_t *node = new_node(is_object ? AUR_JSON_OBJECT : AUR_JSON_ARRAY);
	aur_json_t **tail;
	char close = is_object ? '}' : ']';

	if(!node) {
		ps->err = "out of memory";
		return NULL;
	}
	if(++ps->depth > AUR_JSON_MAXDEPTH) {
		ps->err = "nesting too deep";
		goto fail;
	}
	tail = &node->child;
	ps->p++;
	skip_ws(ps);
	if(ps->p < ps->end && *ps->p == close) {
		ps->p++;
		ps->depth--;
		return node;
	}
	for(;;) {
		char *key = NULL;
		aur_json_t *v;
		skip_ws(ps);
		if(is_object) {
			if(ps->p >= ps->end || *ps->p != '"') {
				ps->err = "expected key";
				goto fail;
			}
			if(!(key = parse_string(ps))) {
				goto fail;
			}
			skip_ws(ps);
			if(ps->p >= ps->end || *ps->p != ':') {
				ps->err = "expected ':'";
				free(key);
				goto fail;
			}
			ps->p++;
		}
		if(!(v = parse_value(ps))) {
			free(key);
			goto fail;
		}
		v->key = key;
		*tail = v;
		tail = &v->next;
		node->count++;
		skip_ws(ps);
		if(ps->p < ps->end && *ps->p == ',') {
			ps->p++;
			continue;
		}
		if(ps->p < ps->end && *ps->p == close) {
			ps->p++;
			break;
		}
		ps->err = is_object ? "expected ',' or '}'" : "expected ',' or ']'";
		goto fail;
	}
	ps->depth--;
	return node;
fail:
	aur_json_free(node);
	return NULL;
}

static int match_word(parser_t *ps, const char *w)
{
	size_t n = strlen(w);
	if((size_t)(ps->end - ps->p) >= n && memcmp(ps->p, w, n) == 0) {
		ps->p += n;
		return 1;
	}
	return 0;
}

static aur_json_t *parse_number(parser_t *ps)
{
	const char *s = ps->p;
	char buf[64];
	size_t n;
	char *endp;
	aur_json_t *node;

	if(ps->p < ps->end && *ps->p == '-') ps->p++;
	if(ps->p >= ps->end || !(*ps->p >= '0' && *ps->p <= '9')) {
		ps->err = "invalid value";
		return NULL;
	}
	while(ps->p < ps->end && ((*ps->p >= '0' && *ps->p <= '9') || *ps->p == '.' ||
				*ps->p == 'e' || *ps->p == 'E' || *ps->p == '+' || *ps->p == '-')) {
		ps->p++;
	}
	n = (size_t)(ps->p - s);
	if(n >= sizeof(buf)) {
		ps->err = "number too long";
		return NULL;
	}
	memcpy(buf, s, n);
	buf[n] = '\0';
	errno = 0;
	node = new_node(AUR_JSON_NUMBER);
	if(!node) {
		ps->err = "out of memory";
		return NULL;
	}
	/* JSON-Zahlen haben immer einen Punkt: nie vom Locale abhängig parsen
	 * (de_DE: Dezimalkomma, strtod("3.47") scheitert sonst). */
	{
		static locale_t cloc;
		if(!cloc) {
			cloc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
		}
		if(cloc) {
			node->number = strtod_l(buf, &endp, cloc);
		} else {
			node->number = strtod(buf, &endp);
		}
	}
	if(*endp != '\0') {
		ps->err = "invalid number";
		free(node);
		return NULL;
	}
	return node;
}

static aur_json_t *parse_value(parser_t *ps)
{
	aur_json_t *node;
	skip_ws(ps);
	if(ps->p >= ps->end) {
		ps->err = "unexpected end of input";
		return NULL;
	}
	switch(*ps->p) {
		case '{':
			return parse_container(ps, 1);
		case '[':
			return parse_container(ps, 0);
		case '"': {
			char *s = parse_string(ps);
			if(!s) {
				return NULL;
			}
			if(!(node = new_node(AUR_JSON_STRING))) {
				free(s);
				ps->err = "out of memory";
				return NULL;
			}
			node->string = s;
			return node;
		}
		default:
			break;
	}
	if(match_word(ps, "true")) {
		node = new_node(AUR_JSON_BOOL);
		if(node) node->number = 1;
	} else if(match_word(ps, "false")) {
		node = new_node(AUR_JSON_BOOL);
	} else if(match_word(ps, "null")) {
		node = new_node(AUR_JSON_NULL);
	} else {
		return parse_number(ps);
	}
	if(!node) {
		ps->err = "out of memory";
	}
	return node;
}

aur_json_t *aur_json_parse(const char *text, size_t len, const char **err)
{
	parser_t ps = { text, text + len, NULL, 0 };
	aur_json_t *root;

	if(!text) {
		if(err) *err = "no input";
		return NULL;
	}
	root = parse_value(&ps);
	if(root) {
		skip_ws(&ps);
		if(ps.p != ps.end) {
			ps.err = "trailing garbage";
			aur_json_free(root);
			root = NULL;
		}
	}
	if(err) {
		*err = root ? NULL : (ps.err ? ps.err : "parse error");
	}
	return root;
}

aur_json_t *aur_json_get(const aur_json_t *obj, const char *key)
{
	if(!obj || obj->type != AUR_JSON_OBJECT || !key) {
		return NULL;
	}
	for(aur_json_t *c = obj->child; c; c = c->next) {
		if(c->key && strcmp(c->key, key) == 0) {
			return c;
		}
	}
	return NULL;
}

aur_json_t *aur_json_index(const aur_json_t *arr, size_t i)
{
	aur_json_t *c;
	if(!arr || (arr->type != AUR_JSON_ARRAY && arr->type != AUR_JSON_OBJECT)) {
		return NULL;
	}
	for(c = arr->child; c && i; c = c->next, i--);
	return c;
}

const char *aur_json_str(const aur_json_t *j)
{
	return (j && j->type == AUR_JSON_STRING) ? j->string : NULL;
}

double aur_json_num(const aur_json_t *j, double fallback)
{
	if(j && (j->type == AUR_JSON_NUMBER || j->type == AUR_JSON_BOOL)) {
		return j->number;
	}
	return fallback;
}

const char *aur_json_get_str(const aur_json_t *obj, const char *key)
{
	return aur_json_str(aur_json_get(obj, key));
}

double aur_json_get_num(const aur_json_t *obj, const char *key, double fallback)
{
	return aur_json_num(aur_json_get(obj, key), fallback);
}

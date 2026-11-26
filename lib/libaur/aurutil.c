/*
 *  util.c - Hilfsfunktionen für libaur
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <alpm.h>

#include "aurutil.h"

char *aur_read_file(const char *path, size_t *len)
{
	FILE *fp = fopen(path, "r");
	char *buf = NULL;
	size_t size = 0, used = 0;

	if(!fp) {
		return NULL;
	}
	for(;;) {
		size_t n;
		if(size - used < 4096) {
			char *nb = realloc(buf, size ? size * 2 : 8192);
			if(!nb) {
				free(buf);
				fclose(fp);
				errno = ENOMEM;
				return NULL;
			}
			buf = nb;
			size = size ? size * 2 : 8192;
		}
		n = fread(buf + used, 1, size - used - 1, fp);
		used += n;
		if(n == 0) {
			break;
		}
	}
	if(ferror(fp)) {
		int e = errno;
		free(buf);
		fclose(fp);
		errno = e;
		return NULL;
	}
	fclose(fp);
	buf[used] = '\0';
	if(len) {
		*len = used;
	}
	return buf;
}

char *aur_dep_name(const char *dep)
{
	size_t n = strcspn(dep, "<>=");
	char *s = malloc(n + 1);
	if(s) {
		memcpy(s, dep, n);
		s[n] = '\0';
	}
	return s;
}

static int version_ok(alpm_depmod_t mod, const char *have, const char *want)
{
	int cmp;
	if(mod == ALPM_DEP_MOD_ANY) {
		return 1;
	}
	if(!have) {
		return 0;
	}
	cmp = alpm_pkg_vercmp(have, want);
	switch(mod) {
		case ALPM_DEP_MOD_EQ: return cmp == 0;
		case ALPM_DEP_MOD_GE: return cmp >= 0;
		case ALPM_DEP_MOD_LE: return cmp <= 0;
		case ALPM_DEP_MOD_GT: return cmp > 0;
		case ALPM_DEP_MOD_LT: return cmp < 0;
		default: return 1;
	}
}

int aur_dep_satisfied(const char *depstr, const char *name, const char *version,
		alpm_list_t *provides)
{
	alpm_depend_t *dep = alpm_dep_from_string(depstr);
	int ok = 0;
	if(!dep) {
		return 0;
	}
	if(name && strcmp(dep->name, name) == 0 && version_ok(dep->mod, version, dep->version)) {
		ok = 1;
	}
	for(alpm_list_t *i = provides; i && !ok; i = i->next) {
		alpm_depend_t *prov = alpm_dep_from_string(i->data);
		if(!prov) {
			continue;
		}
		if(strcmp(prov->name, dep->name) == 0) {
			if(dep->mod == ALPM_DEP_MOD_ANY) {
				ok = 1;
			} else if(prov->mod == ALPM_DEP_MOD_EQ) {
				ok = version_ok(dep->mod, prov->version, dep->version);
			}
		}
		alpm_dep_free(prov);
	}
	alpm_dep_free(dep);
	return ok;
}

alpm_list_t *aur_list_add_unique(alpm_list_t *list, const char *s)
{
	if(!alpm_list_find_str(list, s)) {
		list = alpm_list_add(list, strdup(s));
	}
	return list;
}

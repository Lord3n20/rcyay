/*
 *  srcinfo.c - .SRCINFO-Parser für libaur
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>

#include "srcinfo.h"
#include "aurutil.h"

static void archval_free(void *p)
{
	aur_archval_t *av = p;
	if(av) {
		free(av->arch);
		free(av->value);
		free(av);
	}
}

static void fields_free(aur_si_fields_t *f)
{
	alpm_list_free_inner(f->depends, archval_free);
	alpm_list_free(f->depends);
	alpm_list_free_inner(f->makedepends, archval_free);
	alpm_list_free(f->makedepends);
	alpm_list_free_inner(f->checkdepends, archval_free);
	alpm_list_free(f->checkdepends);
	alpm_list_free_inner(f->optdepends, archval_free);
	alpm_list_free(f->optdepends);
	alpm_list_free_inner(f->provides, archval_free);
	alpm_list_free(f->provides);
	alpm_list_free_inner(f->conflicts, archval_free);
	alpm_list_free(f->conflicts);
	alpm_list_free_inner(f->replaces, archval_free);
	alpm_list_free(f->replaces);
	FREELIST(f->arch);
	free(f->pkgdesc);
}

void aur_srcinfo_free(aur_srcinfo_t *si)
{
	if(!si) {
		return;
	}
	for(alpm_list_t *i = si->pkgs; i; i = i->next) {
		aur_si_pkg_t *p = i->data;
		free(p->name);
		fields_free(&p->f);
		free(p);
	}
	alpm_list_free(si->pkgs);
	fields_free(&si->f);
	free(si->pkgbase);
	free(si->pkgver);
	free(si->pkgrel);
	free(si->epoch);
	free(si->version);
	free(si);
}

static char *trim(char *s)
{
	char *e;
	while(*s == ' ' || *s == '\t') s++;
	e = s + strlen(s);
	while(e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	*e = '\0';
	return s;
}

struct fieldmap {
	const char *name;
	size_t offset;
	unsigned int bit;
};

static const struct fieldmap archfields[] = {
	{ "depends", offsetof(aur_si_fields_t, depends), AUR_SI_DEPENDS },
	{ "makedepends", offsetof(aur_si_fields_t, makedepends), AUR_SI_MAKEDEPENDS },
	{ "checkdepends", offsetof(aur_si_fields_t, checkdepends), AUR_SI_CHECKDEPENDS },
	{ "optdepends", offsetof(aur_si_fields_t, optdepends), AUR_SI_OPTDEPENDS },
	{ "provides", offsetof(aur_si_fields_t, provides), AUR_SI_PROVIDES },
	{ "conflicts", offsetof(aur_si_fields_t, conflicts), AUR_SI_CONFLICTS },
	{ "replaces", offsetof(aur_si_fields_t, replaces), AUR_SI_REPLACES },
	{ NULL, 0, 0 }
};

static int set_err(char **err, int line, const char *msg, const char *extra)
{
	if(err) {
		if(asprintf(err, "SRCINFO line %d: %s%s%s", line, msg,
					extra ? ": " : "", extra ? extra : "") < 0) {
			*err = NULL;
		}
	}
	return -1;
}

/* Wertet "key = value" aus. f = aktueller Abschnitt; in_pkg = pkgname-Abschnitt */
static int handle_field(aur_si_fields_t *f, int in_pkg, const char *key,
		const char *value)
{
	for(const struct fieldmap *m = archfields; m->name; m++) {
		size_t n = strlen(m->name);
		if(strncmp(key, m->name, n) == 0 && (key[n] == '\0' || key[n] == '_')) {
			alpm_list_t **lst = (alpm_list_t **)((char *)f + m->offset);
			aur_archval_t *av;
			if(key[n] == '_' && key[n + 1] == '\0') {
				return 0; /* "depends_" – ungültig, ignorieren */
			}
			if(in_pkg) {
				f->set |= m->bit;
			}
			if(*value == '\0') {
				/* leerer Wert im pkgname-Abschnitt: löscht das Feld */
				return 0;
			}
			av = calloc(1, sizeof(*av));
			if(!av) {
				return -1;
			}
			av->arch = key[n] == '_' ? strdup(key + n + 1) : NULL;
			av->value = strdup(value);
			*lst = alpm_list_add(*lst, av);
			return 0;
		}
	}
	if(strcmp(key, "arch") == 0) {
		if(in_pkg) {
			f->set |= AUR_SI_ARCH;
		}
		if(*value) {
			f->arch = alpm_list_add(f->arch, strdup(value));
		}
	} else if(strcmp(key, "pkgdesc") == 0) {
		if(in_pkg) {
			f->set |= AUR_SI_PKGDESC;
		}
		free(f->pkgdesc);
		f->pkgdesc = strdup(value);
	}
	/* übrige Felder (source, sha256sums, url, license …) braucht rcyay nicht */
	return 0;
}

aur_srcinfo_t *aur_srcinfo_parse(const char *text, char **err)
{
	aur_srcinfo_t *si;
	aur_si_pkg_t *cur = NULL;
	char *buf, *line;
	int lineno = 0;

	if(err) {
		*err = NULL;
	}
	if(!text) {
		set_err(err, 0, "no input", NULL);
		return NULL;
	}
	si = calloc(1, sizeof(*si));
	buf = strdup(text);
	if(!si || !buf) {
		free(si);
		free(buf);
		return NULL;
	}

	line = buf;
	while(line && *line) {
		char *nl = strchr(line, '\n');
		char *k, *v, *eq;
		lineno++;
		if(nl) {
			*nl = '\0';
		}
		k = trim(line);
		if(*k == '\0' || *k == '#') {
			goto next;
		}
		eq = strchr(k, '=');
		if(!eq) {
			set_err(err, lineno, "missing '='", k);
			goto fail;
		}
		*eq = '\0';
		v = trim(eq + 1);
		k = trim(k);
		if(*k == '\0') {
			set_err(err, lineno, "empty key", NULL);
			goto fail;
		}

		if(strcmp(k, "pkgbase") == 0) {
			if(si->pkgbase || cur) {
				set_err(err, lineno, "unexpected pkgbase", v);
				goto fail;
			}
			si->pkgbase = strdup(v);
		} else if(strcmp(k, "pkgname") == 0) {
			if(!si->pkgbase) {
				set_err(err, lineno, "pkgname before pkgbase", v);
				goto fail;
			}
			if(*v == '\0' || aur_srcinfo_find_pkg(si, v)) {
				set_err(err, lineno, "invalid or duplicate pkgname", v);
				goto fail;
			}
			cur = calloc(1, sizeof(*cur));
			if(!cur) {
				goto fail;
			}
			cur->name = strdup(v);
			si->pkgs = alpm_list_add(si->pkgs, cur);
		} else if(!si->pkgbase) {
			set_err(err, lineno, "field before pkgbase", k);
			goto fail;
		} else if(!cur && strcmp(k, "pkgver") == 0) {
			free(si->pkgver);
			si->pkgver = strdup(v);
		} else if(!cur && strcmp(k, "pkgrel") == 0) {
			free(si->pkgrel);
			si->pkgrel = strdup(v);
		} else if(!cur && strcmp(k, "epoch") == 0) {
			free(si->epoch);
			si->epoch = strdup(v);
		} else if(cur && (strncmp(k, "makedepends", 11) == 0 ||
					strncmp(k, "checkdepends", 12) == 0)) {
			/* makepkg erlaubt das nicht pro Paket – ignorieren */
		} else if(handle_field(cur ? &cur->f : &si->f, cur != NULL, k, v) != 0) {
			goto fail;
		}
next:
		line = nl ? nl + 1 : NULL;
	}

	if(!si->pkgbase) {
		set_err(err, lineno, "no pkgbase", NULL);
		goto fail;
	}
	if(!si->pkgs) {
		set_err(err, lineno, "no pkgname", NULL);
		goto fail;
	}
	if(!si->pkgver || !si->pkgrel) {
		set_err(err, lineno, "missing pkgver or pkgrel", NULL);
		goto fail;
	}
	if(asprintf(&si->version, "%s%s%s-%s",
				si->epoch && *si->epoch && strcmp(si->epoch, "0") != 0 ? si->epoch : "",
				si->epoch && *si->epoch && strcmp(si->epoch, "0") != 0 ? ":" : "",
				si->pkgver, si->pkgrel) < 0) {
		si->version = NULL;
		goto fail;
	}
	free(buf);
	return si;

fail:
	free(buf);
	aur_srcinfo_free(si);
	return NULL;
}

aur_srcinfo_t *aur_srcinfo_parse_file(const char *path, char **err)
{
	char *text = aur_read_file(path, NULL);
	aur_srcinfo_t *si;
	if(!text) {
		if(err && asprintf(err, "%s: %s", path, strerror(errno)) < 0) {
			*err = NULL;
		}
		return NULL;
	}
	si = aur_srcinfo_parse(text, err);
	free(text);
	return si;
}

aur_si_pkg_t *aur_srcinfo_find_pkg(aur_srcinfo_t *si, const char *name)
{
	if(!si || !name) {
		return NULL;
	}
	for(alpm_list_t *i = si->pkgs; i; i = i->next) {
		aur_si_pkg_t *p = i->data;
		if(strcmp(p->name, name) == 0) {
			return p;
		}
	}
	return NULL;
}

static alpm_list_t *collect(alpm_list_t *vals, const char *arch, alpm_list_t *out)
{
	for(alpm_list_t *i = vals; i; i = i->next) {
		aur_archval_t *av = i->data;
		if(!av->arch || (arch && strcmp(av->arch, arch) == 0)) {
			out = alpm_list_add(out, av->value);
		}
	}
	return out;
}

alpm_list_t *aur_srcinfo_values(aur_srcinfo_t *si, aur_si_pkg_t *pkg,
		enum aur_si_field field, const char *arch)
{
	const struct fieldmap *m = &archfields[field];
	aur_si_fields_t *f = &si->f;
	if(pkg && (pkg->f.set & m->bit)) {
		f = &pkg->f;
	}
	return collect(*(alpm_list_t **)((char *)f + m->offset), arch, NULL);
}

const char *aur_srcinfo_pkgdesc(aur_srcinfo_t *si, aur_si_pkg_t *pkg)
{
	if(pkg && (pkg->f.set & AUR_SI_PKGDESC)) {
		return pkg->f.pkgdesc;
	}
	return si->f.pkgdesc;
}

int aur_srcinfo_supports_arch(aur_srcinfo_t *si, aur_si_pkg_t *pkg, const char *arch)
{
	alpm_list_t *a = si->f.arch;
	if(pkg && (pkg->f.set & AUR_SI_ARCH)) {
		a = pkg->f.arch;
	}
	if(!a) {
		return 1;
	}
	for(; a; a = a->next) {
		if(strcmp(a->data, "any") == 0 || (arch && strcmp(a->data, arch) == 0)) {
			return 1;
		}
	}
	return 0;
}

/*
 *  srcinfo.h - .SRCINFO-Parser für libaur
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef AUR_SRCINFO_H
#define AUR_SRCINFO_H

#include <alpm_list.h>

/* Ein Wert mit optionaler Architektur (depends_x86_64 = … → arch "x86_64") */
typedef struct {
	char *arch;   /* NULL = alle Architekturen */
	char *value;
} aur_archval_t;

/* Felder, die in pkgbase und pkgname vorkommen können. Ein Feld gilt als
 * "gesetzt", sobald es in einem pkgname-Abschnitt auftaucht (auch leer);
 * dann ersetzt es den pkgbase-Wert vollständig (wie bei makepkg). */
typedef struct {
	alpm_list_t *depends;       /* aur_archval_t* */
	alpm_list_t *makedepends;
	alpm_list_t *checkdepends;
	alpm_list_t *optdepends;
	alpm_list_t *provides;
	alpm_list_t *conflicts;
	alpm_list_t *replaces;
	alpm_list_t *arch;          /* char* */
	char *pkgdesc;
	unsigned int set;           /* Bitmaske AUR_SI_* (nur in pkgname-Abschnitten) */
} aur_si_fields_t;

enum {
	AUR_SI_DEPENDS = 1 << 0,
	AUR_SI_MAKEDEPENDS = 1 << 1,
	AUR_SI_CHECKDEPENDS = 1 << 2,
	AUR_SI_OPTDEPENDS = 1 << 3,
	AUR_SI_PROVIDES = 1 << 4,
	AUR_SI_CONFLICTS = 1 << 5,
	AUR_SI_REPLACES = 1 << 6,
	AUR_SI_ARCH = 1 << 7,
	AUR_SI_PKGDESC = 1 << 8
};

typedef struct {
	char *name;
	aur_si_fields_t f;
} aur_si_pkg_t;

typedef struct {
	char *pkgbase;
	char *pkgver;
	char *pkgrel;
	char *epoch;
	char *version;              /* [epoch:]pkgver-pkgrel */
	aur_si_fields_t f;
	alpm_list_t *pkgs;          /* aur_si_pkg_t* */
} aur_srcinfo_t;

/* Parst den Inhalt einer .SRCINFO. Bei Fehler NULL, *err zeigt auf eine
 * Meldung (malloc, vom Aufrufer freizugeben). */
aur_srcinfo_t *aur_srcinfo_parse(const char *text, char **err);
aur_srcinfo_t *aur_srcinfo_parse_file(const char *path, char **err);
void aur_srcinfo_free(aur_srcinfo_t *si);

aur_si_pkg_t *aur_srcinfo_find_pkg(aur_srcinfo_t *si, const char *name);

/* Wirksame Werte eines Pakets für eine Architektur (Vererbung von pkgbase
 * und *_ARCH berücksichtigt). Liefert eine neue Liste von char* (Strings
 * gehören weiter der Struktur, nur alpm_list_free() aufrufen). */
enum aur_si_field {
	AUR_FIELD_DEPENDS,
	AUR_FIELD_MAKEDEPENDS,
	AUR_FIELD_CHECKDEPENDS,
	AUR_FIELD_OPTDEPENDS,
	AUR_FIELD_PROVIDES,
	AUR_FIELD_CONFLICTS,
	AUR_FIELD_REPLACES
};
alpm_list_t *aur_srcinfo_values(aur_srcinfo_t *si, aur_si_pkg_t *pkg,
		enum aur_si_field field, const char *arch);
const char *aur_srcinfo_pkgdesc(aur_srcinfo_t *si, aur_si_pkg_t *pkg);
/* 1, wenn das Paket für arch gebaut werden kann ("any" oder arch in arch=()) */
int aur_srcinfo_supports_arch(aur_srcinfo_t *si, aur_si_pkg_t *pkg, const char *arch);

#endif

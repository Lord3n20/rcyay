/*
 *  util.h - Hilfsfunktionen für libaur
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef AUR_AURUTIL_H
#define AUR_AURUTIL_H

#include <stddef.h>
#include <alpm_list.h>

#ifndef FREELIST
#define FREELIST(p) do { alpm_list_free_inner(p, free); alpm_list_free(p); p = NULL; } while(0)
#endif

/* Liest eine Datei vollständig (NUL-terminiert). */
char *aur_read_file(const char *path, size_t *len);

/* Prüft, ob ein Paket name/version mit provides (Liste von char* wie
 * "foo=1.2" oder "foo") die Abhängigkeit dep ("foo>=1.0") erfüllt.
 * Regeln wie libalpm: ein provides ohne Version erfüllt nur
 * unversionierte Abhängigkeiten. */
int aur_dep_satisfied(const char *dep, const char *name, const char *version,
		alpm_list_t *provides);

/* Name einer Abhängigkeit ohne Versionsangabe (neuer String). */
char *aur_dep_name(const char *dep);

/* Anhängen, falls noch nicht vorhanden (strdup). */
alpm_list_t *aur_list_add_unique(alpm_list_t *list, const char *s);

#endif

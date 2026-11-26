/*
 *  rpc.h - AUR-RPC v5, Downloads und Snapshots für libaur
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef AUR_RPC_H
#define AUR_RPC_H

#include <stddef.h>
#include <alpm_list.h>

typedef struct {
	char *name;
	char *pkgbase;
	char *version;
	char *desc;
	char *url;
	char *urlpath;      /* Snapshot-Pfad relativ zur AUR-Basis-URL */
	char *maintainer;
	char *submitter;
	long votes;
	double popularity;
	long outofdate;     /* 0 = nicht veraltet, sonst Zeitstempel */
	long firstsubmitted;
	long lastmodified;
	alpm_list_t *depends;
	alpm_list_t *makedepends;
	alpm_list_t *checkdepends;
	alpm_list_t *optdepends;
	alpm_list_t *provides;
	alpm_list_t *conflicts;
	alpm_list_t *replaces;
	alpm_list_t *groups;
	alpm_list_t *licenses;
	alpm_list_t *keywords;
	alpm_list_t *comaintainers;
} aur_pkg_t;

typedef struct {
	char *baseurl;      /* z. B. https://aur.archlinux.org (ohne / am Ende) */
	char *useragent;
	long timeout;       /* Sekunden */
	char error[512];    /* letzte Fehlermeldung */
} aur_ctx_t;

aur_ctx_t *aur_ctx_new(const char *baseurl, const char *useragent);
void aur_ctx_free(aur_ctx_t *ctx);

void aur_pkg_free(aur_pkg_t *pkg);
void aur_pkglist_free(alpm_list_t *pkgs);
aur_pkg_t *aur_pkglist_find(alpm_list_t *pkgs, const char *name);

/* Parst eine RPC-Antwort. 0 = ok (*out = Liste aur_pkg_t*, evtl. leer),
 * -1 = Fehler (*err malloc'd Meldung). */
int aur_rpc_parse(const char *json, size_t len, alpm_list_t **out, char **err);

/* HTTP-GET in den Speicher. 0 = ok (HTTP 200). Bei HTTP-Fehlern -1,
 * *status enthält dann den Code (0 = Verbindungsfehler). */
int aur_http_get(aur_ctx_t *ctx, const char *url, char **body, size_t *len, long *status);
/* HTTP-GET in eine Datei (atomar über NAME.part). */
int aur_http_download(aur_ctx_t *ctx, const char *url, const char *path, long *status);

/* RPC info für viele Namen (wird in Teilanfragen zerlegt). Nicht gefundene
 * Namen fehlen einfach im Ergebnis. */
int aur_info(aur_ctx_t *ctx, alpm_list_t *names, alpm_list_t **out);
/* RPC search; by = name, name-desc, maintainer, depends, makedepends,
 * optdepends, checkdepends, provides … (NULL = name-desc). */
int aur_search(aur_ctx_t *ctx, const char *term, const char *by, alpm_list_t **out);

/* Snapshot-URL eines Pakets (malloc). */
char *aur_snapshot_url(aur_ctx_t *ctx, const aur_pkg_t *pkg);
/* URL einer Datei im AUR-Git (cgit plain), z. B. PKGBUILD. */
char *aur_plain_url(aur_ctx_t *ctx, const char *pkgbase, const char *file);

/* Entpackt ein Archiv nach dest (wird angelegt). strip = Anzahl führender
 * Pfadteile, die entfernt werden. Absolute Pfade und ".." werden abgelehnt. */
int aur_extract(const char *archive, const char *dest, int strip, char *err, size_t errlen);

#endif

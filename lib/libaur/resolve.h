/*
 *  resolve.h - Abhängigkeitsgraph für AUR-Pakete (libaur)
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef AUR_RESOLVE_H
#define AUR_RESOLVE_H

#include <alpm_list.h>
#include "rpc.h"
#include "srcinfo.h"

typedef enum {
	AUR_EDGE_RUN = 0,   /* depends */
	AUR_EDGE_MAKE       /* makedepends, checkdepends */
} aur_edge_type_t;

typedef struct aur_base_t aur_base_t;

typedef struct {
	aur_edge_type_t type;
	char *from_pkg;     /* NULL bei Kanten des pkgbase (makedepends) */
	char *dep;          /* Abhängigkeit wie angegeben */
	aur_base_t *to;     /* AUR-Ziel oder NULL */
	char *to_pkg;       /* Paketname im Ziel bzw. Repo-Paketname */
} aur_edge_t;

struct aur_base_t {
	char *pkgbase;
	aur_srcinfo_t *srcinfo;
	aur_pkg_t *rpc;          /* RPC-Eintrag eines der Pakete (gehört dem Aufrufer) */
	alpm_list_t *pkgnames;   /* gewünschte Pakete (char*) */
	alpm_list_t *explicit;   /* davon ausdrücklich verlangt (char*) */
	alpm_list_t *runtime;    /* davon zur Laufzeit gebraucht (char*), Rest = nur Bau */
	alpm_list_t *edges;      /* aur_edge_t* */
	int mark;                /* intern */
};

typedef struct {
	alpm_list_t *bases;      /* aur_base_t* in Bauordnung (Abhängigkeiten zuerst) */
	alpm_list_t *repo;       /* char*: benötigte Repo-Pakete */
	alpm_list_t *repo_make;  /* char*: davon nur zum Bauen gebraucht */
	alpm_list_t *errors;     /* char*: Fehlermeldungen (leer = Plan gültig) */
} aur_plan_t;

typedef struct {
	void *ud;
	const char *arch;
	int nocheck;             /* checkdepends ignorieren */
	/* Abhängigkeit schon installiert? */
	int (*installed)(void *ud, const char *dep);
	/* Repo-Paket, das dep erfüllt (malloc'd Name) oder NULL */
	char *(*repo)(void *ud, const char *dep);
	/* AUR-Paket mit genau diesem Namen oder NULL (gehört dem Aufrufer) */
	aur_pkg_t *(*info)(void *ud, const char *name);
	/* AUR-Pakete, die name bereitstellen (Liste gehört dem Resolver,
	 * Einträge dem Aufrufer) */
	alpm_list_t *(*providers)(void *ud, const char *name);
	/* .SRCINFO eines Pakets holen (Besitz geht an den Plan) */
	aur_srcinfo_t *(*srcinfo)(void *ud, aur_pkg_t *pkg, char **err);
	/* Auswahl bei mehreren Anbietern; NULL = Standardwahl */
	aur_pkg_t *(*choose)(void *ud, const char *dep, alpm_list_t *candidates);
} aur_resolver_t;

/* Löst targets (AUR-Paketnamen) auf. Gibt immer einen Plan zurück (außer bei
 * Speichermangel); plan->errors sagt, ob er gültig ist. */
aur_plan_t *aur_resolve(aur_resolver_t *r, alpm_list_t *targets);
void aur_plan_free(aur_plan_t *plan);

aur_base_t *aur_plan_find_base(aur_plan_t *plan, const char *pkgbase);
/* 1, wenn pkgname im Plan nur zum Bauen gebraucht wird */
int aur_plan_is_makeonly(aur_plan_t *plan, aur_base_t *base, const char *pkgname);

/* Standardwahl unter mehreren Anbietern: gleicher Name, sonst meiste Stimmen */
aur_pkg_t *aur_default_choice(const char *depname, alpm_list_t *candidates);

#endif

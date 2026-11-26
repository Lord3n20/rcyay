/*
 *  resolve.c - Abhängigkeitsgraph für AUR-Pakete (libaur)
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
#include <stdarg.h>

#include "resolve.h"
#include "aurutil.h"
#include "aurintl.h"

typedef struct {
	aur_base_t *base;
	char *pkgname;
} work_t;

typedef struct {
	aur_resolver_t *r;
	aur_plan_t *plan;
	alpm_list_t *all;    /* alle Basen in Einfügeordnung */
	alpm_list_t *queue;  /* work_t* */
	alpm_list_t *failed; /* char*: pkgbases, deren SRCINFO fehlte */
} state_t;

static void add_error(aur_plan_t *plan, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void add_error(aur_plan_t *plan, const char *fmt, ...)
{
	char *s = NULL;
	va_list ap;
	va_start(ap, fmt);
	if(vasprintf(&s, fmt, ap) >= 0) {
		if(alpm_list_find_str(plan->errors, s)) {
			free(s);
		} else {
			plan->errors = alpm_list_add(plan->errors, s);
		}
	}
	va_end(ap);
}

static void edge_free(void *p)
{
	aur_edge_t *e = p;
	free(e->from_pkg);
	free(e->dep);
	free(e->to_pkg);
	free(e);
}

static void base_free(aur_base_t *b)
{
	free(b->pkgbase);
	aur_srcinfo_free(b->srcinfo);
	FREELIST(b->pkgnames);
	FREELIST(b->explicit);
	FREELIST(b->runtime);
	alpm_list_free_inner(b->edges, edge_free);
	alpm_list_free(b->edges);
	free(b);
}

void aur_plan_free(aur_plan_t *plan)
{
	if(!plan) {
		return;
	}
	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		base_free(i->data);
	}
	alpm_list_free(plan->bases);
	FREELIST(plan->repo);
	FREELIST(plan->repo_make);
	FREELIST(plan->errors);
	free(plan);
}

aur_base_t *aur_plan_find_base(aur_plan_t *plan, const char *pkgbase)
{
	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		aur_base_t *b = i->data;
		if(strcmp(b->pkgbase, pkgbase) == 0) {
			return b;
		}
	}
	return NULL;
}

int aur_plan_is_makeonly(aur_plan_t *plan, aur_base_t *base, const char *pkgname)
{
	(void)plan;
	return alpm_list_find_str(base->pkgnames, pkgname) &&
		!alpm_list_find_str(base->runtime, pkgname);
}

aur_pkg_t *aur_default_choice(const char *depname, alpm_list_t *cands)
{
	aur_pkg_t *best = NULL;
	for(alpm_list_t *i = cands; i; i = i->next) {
		aur_pkg_t *p = i->data;
		if(strcmp(p->name, depname) == 0) {
			return p;
		}
		if(!best || p->votes > best->votes) {
			best = p;
		}
	}
	return best;
}

static aur_base_t *find_base(state_t *st, const char *pkgbase)
{
	for(alpm_list_t *i = st->all; i; i = i->next) {
		aur_base_t *b = i->data;
		if(strcmp(b->pkgbase, pkgbase) == 0) {
			return b;
		}
	}
	return NULL;
}

/* Paket pkgname (aus RPC-Eintrag pkg) in den Plan aufnehmen. */
static aur_base_t *add_pkg(state_t *st, aur_pkg_t *pkg, const char *pkgname, int explicit)
{
	aur_base_t *b = find_base(st, pkg->pkgbase);
	if(!b) {
		char *err = NULL;
		aur_srcinfo_t *si;
		if(alpm_list_find_str(st->failed, pkg->pkgbase)) {
			return NULL;
		}
		si = st->r->srcinfo(st->r->ud, pkg, &err);
		if(!si) {
			add_error(st->plan, _("%s: cannot read .SRCINFO: %s"), pkg->pkgbase,
					err ? err : _("unknown error"));
			free(err);
			st->failed = alpm_list_add(st->failed, strdup(pkg->pkgbase));
			return NULL;
		}
		free(err);
		b = calloc(1, sizeof(*b));
		if(!b) {
			aur_srcinfo_free(si);
			return NULL;
		}
		b->pkgbase = strdup(pkg->pkgbase);
		b->srcinfo = si;
		b->rpc = pkg;
		st->all = alpm_list_add(st->all, b);
	}
	if(!aur_srcinfo_find_pkg(b->srcinfo, pkgname)) {
		add_error(st->plan, _("%s: package %s missing in .SRCINFO"), b->pkgbase, pkgname);
		return NULL;
	}
	if(!aur_srcinfo_supports_arch(b->srcinfo, aur_srcinfo_find_pkg(b->srcinfo, pkgname),
				st->r->arch)) {
		add_error(st->plan, _("%s: not available for architecture %s"), pkgname,
				st->r->arch ? st->r->arch : "?");
	}
	if(explicit) {
		b->explicit = aur_list_add_unique(b->explicit, pkgname);
	}
	if(!alpm_list_find_str(b->pkgnames, pkgname)) {
		work_t *w = calloc(1, sizeof(*w));
		b->pkgnames = alpm_list_add(b->pkgnames, strdup(pkgname));
		if(w) {
			w->base = b;
			w->pkgname = strdup(pkgname);
			st->queue = alpm_list_add(st->queue, w);
		}
		if(alpm_list_count(b->pkgnames) == 1) {
			/* erstes Paket dieses pkgbase: Bauabhängigkeiten einreihen */
			work_t *wm = calloc(1, sizeof(*wm));
			if(wm) {
				wm->base = b;
				wm->pkgname = NULL;
				st->queue = alpm_list_add(st->queue, wm);
			}
		}
	}
	return b;
}

/* Sucht ein Paket im Plan (auch noch nicht gewünschte Teilpakete eines
 * bereits aufgenommenen pkgbase), das dep erfüllt. */
static aur_base_t *plan_satisfier(state_t *st, const char *dep, const char **pkgname)
{
	for(alpm_list_t *i = st->all; i; i = i->next) {
		aur_base_t *b = i->data;
		for(alpm_list_t *j = b->srcinfo->pkgs; j; j = j->next) {
			aur_si_pkg_t *p = j->data;
			alpm_list_t *prov = aur_srcinfo_values(b->srcinfo, p, AUR_FIELD_PROVIDES, st->r->arch);
			int ok = aur_dep_satisfied(dep, p->name, b->srcinfo->version, prov);
			alpm_list_free(prov);
			if(ok) {
				*pkgname = p->name;
				return b;
			}
		}
	}
	return NULL;
}

static int rpc_satisfies(aur_pkg_t *p, const char *dep)
{
	return aur_dep_satisfied(dep, p->name, p->version, p->provides);
}

static aur_pkg_t *find_aur(state_t *st, const char *dep)
{
	char *name = aur_dep_name(dep);
	aur_pkg_t *p = NULL;
	alpm_list_t *cands = NULL, *provs;

	if(!name) {
		return NULL;
	}
	p = st->r->info(st->r->ud, name);
	if(p && rpc_satisfies(p, dep)) {
		free(name);
		return p;
	}
	p = NULL;
	provs = st->r->providers ? st->r->providers(st->r->ud, name) : NULL;
	for(alpm_list_t *i = provs; i; i = i->next) {
		if(rpc_satisfies(i->data, dep)) {
			cands = alpm_list_add(cands, i->data);
		}
	}
	alpm_list_free(provs);
	if(cands) {
		if(cands->next && st->r->choose) {
			p = st->r->choose(st->r->ud, dep, cands);
		}
		if(!p) {
			p = aur_default_choice(name, cands);
		}
	}
	alpm_list_free(cands);
	free(name);
	return p;
}

static void add_edge(aur_base_t *from, aur_edge_type_t type, const char *from_pkg,
		const char *dep, aur_base_t *to, const char *to_pkg)
{
	aur_edge_t *e = calloc(1, sizeof(*e));
	if(!e) {
		return;
	}
	e->type = type;
	e->from_pkg = from_pkg ? strdup(from_pkg) : NULL;
	e->dep = strdup(dep);
	e->to = to;
	e->to_pkg = to_pkg ? strdup(to_pkg) : NULL;
	from->edges = alpm_list_add(from->edges, e);
}

static void resolve_dep(state_t *st, aur_base_t *b, const char *from_pkg,
		const char *dep, aur_edge_type_t type)
{
	const char *pname = NULL;
	aur_base_t *t;
	char *repo;
	aur_pkg_t *ap;

	/* 1. ein Paket des Plans (geht vor installierten, damit in richtiger
	 *    Reihenfolge gebaut wird, wenn es ohnehin neu gebaut wird) */
	t = plan_satisfier(st, dep, &pname);
	if(t) {
		add_pkg(st, t->rpc, pname, 0);
		add_edge(b, type, from_pkg, dep, t, pname);
		return;
	}
	/* 2. schon installiert */
	if(st->r->installed && st->r->installed(st->r->ud, dep)) {
		return;
	}
	/* 3. Repo */
	if(st->r->repo && (repo = st->r->repo(st->r->ud, dep))) {
		add_edge(b, type, from_pkg, dep, NULL, repo);
		st->plan->repo = aur_list_add_unique(st->plan->repo, repo);
		free(repo);
		return;
	}
	/* 4. AUR */
	ap = find_aur(st, dep);
	if(ap) {
		t = add_pkg(st, ap, ap->name, 0);
		if(t) {
			add_edge(b, type, from_pkg, dep, t, ap->name);
		}
		return;
	}
	add_error(st->plan, _("dependency not found: %s (required by %s)"), dep,
			from_pkg ? from_pkg : b->pkgbase);
}

static void process(state_t *st, work_t *w)
{
	aur_srcinfo_t *si = w->base->srcinfo;
	alpm_list_t *deps;
	if(w->pkgname) {
		aur_si_pkg_t *p = aur_srcinfo_find_pkg(si, w->pkgname);
		deps = aur_srcinfo_values(si, p, AUR_FIELD_DEPENDS, st->r->arch);
		for(alpm_list_t *i = deps; i; i = i->next) {
			resolve_dep(st, w->base, w->pkgname, i->data, AUR_EDGE_RUN);
		}
		alpm_list_free(deps);
	} else {
		deps = aur_srcinfo_values(si, NULL, AUR_FIELD_MAKEDEPENDS, st->r->arch);
		for(alpm_list_t *i = deps; i; i = i->next) {
			resolve_dep(st, w->base, NULL, i->data, AUR_EDGE_MAKE);
		}
		alpm_list_free(deps);
		if(!st->r->nocheck) {
			deps = aur_srcinfo_values(si, NULL, AUR_FIELD_CHECKDEPENDS, st->r->arch);
			for(alpm_list_t *i = deps; i; i = i->next) {
				resolve_dep(st, w->base, NULL, i->data, AUR_EDGE_MAKE);
			}
			alpm_list_free(deps);
		}
	}
}

/* Laufzeitbedarf: von ausdrücklichen Zielen über depends-Kanten erreichbar */
static void mark_runtime(state_t *st)
{
	int changed = 1;
	alpm_list_t *repo_run = NULL;

	for(alpm_list_t *i = st->all; i; i = i->next) {
		aur_base_t *b = i->data;
		for(alpm_list_t *j = b->explicit; j; j = j->next) {
			b->runtime = aur_list_add_unique(b->runtime, j->data);
		}
	}
	while(changed) {
		changed = 0;
		for(alpm_list_t *i = st->all; i; i = i->next) {
			aur_base_t *b = i->data;
			for(alpm_list_t *j = b->edges; j; j = j->next) {
				aur_edge_t *e = j->data;
				if(e->type != AUR_EDGE_RUN || !alpm_list_find_str(b->runtime, e->from_pkg)) {
					continue;
				}
				if(e->to) {
					if(!alpm_list_find_str(e->to->runtime, e->to_pkg)) {
						e->to->runtime = alpm_list_add(e->to->runtime, strdup(e->to_pkg));
						changed = 1;
					}
				} else {
					repo_run = aur_list_add_unique(repo_run, e->to_pkg);
				}
			}
		}
	}
	for(alpm_list_t *i = st->plan->repo; i; i = i->next) {
		if(!alpm_list_find_str(repo_run, i->data)) {
			st->plan->repo_make = alpm_list_add(st->plan->repo_make, strdup(i->data));
		}
	}
	FREELIST(repo_run);
}

/* Topologische Sortierung (Tiefensuche); mark: 0 neu, 1 in Arbeit, 2 fertig */
static int visit(state_t *st, aur_base_t *b, alpm_list_t **path)
{
	if(b->mark == 2) {
		return 0;
	}
	if(b->mark == 1) {
		/* Zyklus: Pfad ab b ausgeben */
		char *msg = strdup(_("dependency cycle:")), *n;
		int on = 0;
		for(alpm_list_t *i = *path; i; i = i->next) {
			aur_base_t *x = i->data;
			if(x == b) {
				on = 1;
			}
			if(on && asprintf(&n, "%s %s ->", msg, x->pkgbase) >= 0) {
				free(msg);
				msg = n;
			}
		}
		add_error(st->plan, "%s %s", msg, b->pkgbase);
		free(msg);
		return -1;
	}
	b->mark = 1;
	*path = alpm_list_add(*path, b);
	for(alpm_list_t *i = b->edges; i; i = i->next) {
		aur_edge_t *e = i->data;
		if(e->to && e->to != b && visit(st, e->to, path) != 0) {
			return -1;
		}
	}
	{
		alpm_list_t *last = alpm_list_last(*path);
		*path = alpm_list_remove_item(*path, last);
		free(last);
	}
	b->mark = 2;
	st->plan->bases = alpm_list_add(st->plan->bases, b);
	return 0;
}

aur_plan_t *aur_resolve(aur_resolver_t *r, alpm_list_t *targets)
{
	state_t st = { 0 };
	alpm_list_t *path = NULL;

	st.r = r;
	st.plan = calloc(1, sizeof(aur_plan_t));
	if(!st.plan) {
		return NULL;
	}
	/* Ziele zuerst alle aufnehmen, damit Abhängigkeiten zwischen ihnen
	 * als Kanten erkannt werden */
	for(alpm_list_t *i = targets; i; i = i->next) {
		const char *t = i->data;
		aur_pkg_t *p = r->info(r->ud, t);
		if(!p) {
			alpm_list_t *provs = r->providers ? r->providers(r->ud, t) : NULL;
			if(provs) {
				if(provs->next && r->choose) {
					p = r->choose(r->ud, t, provs);
				}
				if(!p) {
					p = aur_default_choice(t, provs);
				}
			}
			alpm_list_free(provs);
		}
		if(!p) {
			add_error(st.plan, _("target not found in AUR: %s"), t);
			continue;
		}
		add_pkg(&st, p, p->name, 1);
	}
	while(st.queue) {
		alpm_list_t *head = st.queue;
		work_t *w = head->data;
		st.queue = alpm_list_remove_item(st.queue, head);
		free(head);
		process(&st, w);
		free(w->pkgname);
		free(w);
	}
	mark_runtime(&st);
	for(alpm_list_t *i = st.all; i; i = i->next) {
		if(visit(&st, i->data, &path) != 0) {
			break;
		}
	}
	alpm_list_free(path);
	/* Basen, die durch einen Zyklus nicht einsortiert wurden, trotzdem
	 * besitzen, damit sie freigegeben werden */
	for(alpm_list_t *i = st.all; i; i = i->next) {
		aur_base_t *b = i->data;
		if(b->mark != 2) {
			b->mark = 2;
			st.plan->bases = alpm_list_add(st.plan->bases, b);
		}
	}
	alpm_list_free(st.all);
	FREELIST(st.failed);
	return st.plan;
}

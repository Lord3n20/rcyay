#define _GNU_SOURCE
#include <stdlib.h>
#include "check.h"
#include "resolve.h"
#include "aurutil.h"

/* Kleine erfundene Welt: installiert, Repo, AUR (RPC + .SRCINFO) */
static const char *installed_pkgs[] = { "glibc", NULL };
static const char *repo_pkgs[] = { "zlib", "cmake", NULL };

struct fake { const char *name, *base, *version, *provides, *srcinfo; };
static const struct fake aur[] = {
	{ "app", "app", "1.0-1", NULL,
		"pkgbase = app\npkgver = 1.0\npkgrel = 1\narch = x86_64\n"
		"depends = lib-a\ndepends = zlib\ndepends = glibc\nmakedepends = builder\nmakedepends = cmake\n"
		"pkgname = app\n" },
	{ "lib-a", "liba-split", "3-1", NULL,
		"pkgbase = liba-split\npkgver = 3\npkgrel = 1\narch = any\n"
		"pkgname = lib-a\ndepends = lib-b>=2\n"
		"pkgname = lib-a-docs\n" },
	{ "lib-a-docs", "liba-split", "3-1", NULL, NULL },
	{ "lib-b-old", "lib-b-old", "1.0-1", "lib-b=1.0",
		"pkgbase = lib-b-old\npkgver = 1.0\npkgrel = 1\nprovides = lib-b=1.0\npkgname = lib-b-old\n" },
	{ "lib-b-git", "lib-b-git", "r10-1", "lib-b=2.5",
		"pkgbase = lib-b-git\npkgver = r10\npkgrel = 1\nprovides = lib-b=2.5\npkgname = lib-b-git\n" },
	{ "builder", "builder", "1-1", NULL,
		"pkgbase = builder\npkgver = 1\npkgrel = 1\ndepends = lib-c\npkgname = builder\n" },
	{ "lib-c", "lib-c", "1-1", NULL,
		"pkgbase = lib-c\npkgver = 1\npkgrel = 1\ndepends = glibc\npkgname = lib-c\n" },
	{ "cyc1", "cyc1", "1-1", NULL,
		"pkgbase = cyc1\npkgver = 1\npkgrel = 1\ndepends = cyc2\npkgname = cyc1\n" },
	{ "cyc2", "cyc2", "1-1", NULL,
		"pkgbase = cyc2\npkgver = 1\npkgrel = 1\nmakedepends = cyc1\npkgname = cyc2\n" },
	{ "broken", "broken", "1-1", NULL,
		"pkgbase = broken\npkgver = 1\npkgrel = 1\ndepends = nothere>=1\npkgname = broken\n" },
	{ "tgt2", "tgt2", "1-1", NULL,
		"pkgbase = tgt2\npkgver = 1\npkgrel = 1\ndepends = lib-a-docs\npkgname = tgt2\n" },
	{ "armonly", "armonly", "1-1", NULL,
		"pkgbase = armonly\npkgver = 1\npkgrel = 1\narch = aarch64\npkgname = armonly\n" },
	{ "nosrc", "nosrc", "1-1", NULL, NULL },
	{ NULL, NULL, NULL, NULL, NULL }
};

static alpm_list_t *rpcpkgs;
static int srcinfo_calls;

static int cb_installed(void *ud, const char *dep)
{
	(void)ud;
	for(const char **p = installed_pkgs; *p; p++) {
		if(aur_dep_satisfied(dep, *p, "1-1", NULL)) return 1;
	}
	return 0;
}

static char *cb_repo(void *ud, const char *dep)
{
	(void)ud;
	for(const char **p = repo_pkgs; *p; p++) {
		if(aur_dep_satisfied(dep, *p, "1-1", NULL)) return strdup(*p);
	}
	return NULL;
}

static aur_pkg_t *cb_info(void *ud, const char *name)
{
	(void)ud;
	return aur_pkglist_find(rpcpkgs, name);
}

static alpm_list_t *cb_providers(void *ud, const char *name)
{
	alpm_list_t *r = NULL;
	(void)ud;
	for(alpm_list_t *i = rpcpkgs; i; i = i->next) {
		aur_pkg_t *p = i->data;
		for(alpm_list_t *j = p->provides; j; j = j->next) {
			char *n = aur_dep_name(j->data);
			if(strcmp(n, name) == 0) r = alpm_list_add(r, p);
			free(n);
		}
	}
	return r;
}

static aur_srcinfo_t *cb_srcinfo(void *ud, aur_pkg_t *pkg, char **err)
{
	(void)ud;
	srcinfo_calls++;
	for(const struct fake *f = aur; f->name; f++) {
		if(strcmp(f->base, pkg->pkgbase) == 0 && f->srcinfo) {
			return aur_srcinfo_parse(f->srcinfo, err);
		}
	}
	*err = strdup("snapshot missing");
	return NULL;
}

static aur_resolver_t R = {
	NULL, "x86_64", 0, cb_installed, cb_repo, cb_info, cb_providers, cb_srcinfo, NULL
};

static aur_plan_t *resolve(const char *t1, const char *t2)
{
	alpm_list_t *t = NULL;
	aur_plan_t *plan;
	t = alpm_list_add(t, (void *)t1);
	if(t2) t = alpm_list_add(t, (void *)t2);
	plan = aur_resolve(&R, t);
	alpm_list_free(t);
	for(alpm_list_t *i = plan->errors; i; i = i->next) {
		printf("# Fehler: %s\n", (char *)i->data);
	}
	return plan;
}

static int pos(aur_plan_t *plan, const char *base)
{
	int n = 0;
	for(alpm_list_t *i = plan->bases; i; i = i->next, n++) {
		if(strcmp(((aur_base_t *)i->data)->pkgbase, base) == 0) return n;
	}
	return -1;
}

static int has_error(aur_plan_t *plan, const char *needle)
{
	for(alpm_list_t *i = plan->errors; i; i = i->next) {
		if(strstr(i->data, needle)) return 1;
	}
	return 0;
}

int main(void)
{
	aur_plan_t *plan;
	aur_base_t *b;

	for(const struct fake *f = aur; f->name; f++) {
		aur_pkg_t *p = calloc(1, sizeof(*p));
		p->name = strdup(f->name);
		p->pkgbase = strdup(f->base);
		p->version = strdup(f->version);
		if(f->provides) p->provides = alpm_list_add(NULL, strdup(f->provides));
		p->votes = strcmp(f->name, "lib-b-git") == 0 ? 100 : (long)strlen(f->name);
		rpcpkgs = alpm_list_add(rpcpkgs, p);
	}

	/* Kette app → lib-a (split) → lib-b (nur über provides) + Bauabhängigkeiten */
	plan = resolve("app", NULL);
	CHECK(plan->errors == NULL);
	CHECK(alpm_list_count(plan->bases) == 5);
	CHECK(pos(plan, "lib-b-git") >= 0 && pos(plan, "lib-b-git") < pos(plan, "liba-split"));
	CHECK(pos(plan, "liba-split") < pos(plan, "app"));
	CHECK(pos(plan, "lib-c") < pos(plan, "builder") && pos(plan, "builder") < pos(plan, "app"));
	CHECK(pos(plan, "lib-b-old") == -1);
	CHECK(alpm_list_count(plan->repo) == 2);
	CHECK(alpm_list_find_str(plan->repo_make, "cmake") && !alpm_list_find_str(plan->repo_make, "zlib"));
	b = aur_plan_find_base(plan, "app");
	CHECK(b && alpm_list_find_str(b->explicit, "app") && !aur_plan_is_makeonly(plan, b, "app"));
	b = aur_plan_find_base(plan, "liba-split");
	CHECK(b && alpm_list_count(b->pkgnames) == 1 && !aur_plan_is_makeonly(plan, b, "lib-a"));
	CHECK(b && b->explicit == NULL);
	b = aur_plan_find_base(plan, "lib-b-git");
	CHECK(b && !aur_plan_is_makeonly(plan, b, "lib-b-git"));
	b = aur_plan_find_base(plan, "builder");
	CHECK(b && aur_plan_is_makeonly(plan, b, "builder"));
	b = aur_plan_find_base(plan, "lib-c");
	CHECK(b && aur_plan_is_makeonly(plan, b, "lib-c"));
	aur_plan_free(plan);

	/* zweites Teilpaket desselben pkgbase; Ziele untereinander */
	plan = resolve("tgt2", "app");
	CHECK(plan->errors == NULL);
	b = aur_plan_find_base(plan, "liba-split");
	CHECK(b && alpm_list_count(b->pkgnames) == 2);
	CHECK(alpm_list_count(plan->bases) == 6);
	CHECK(pos(plan, "liba-split") < pos(plan, "tgt2"));
	aur_plan_free(plan);

	/* Ziel nur über provides */
	plan = resolve("lib-b", NULL);
	CHECK(plan->errors == NULL && alpm_list_count(plan->bases) == 1 && pos(plan, "lib-b-git") == 0);
	aur_plan_free(plan);

	/* Zyklus (auch über makedepends) */
	plan = resolve("cyc1", NULL);
	CHECK(has_error(plan, "cycle"));
	CHECK(alpm_list_count(plan->bases) == 2);
	aur_plan_free(plan);

	/* fehlende Abhängigkeit, unbekanntes Ziel, Architektur, fehlender Snapshot */
	plan = resolve("broken", NULL);
	CHECK(has_error(plan, "dependency not found: nothere>=1 (required by broken)"));
	aur_plan_free(plan);
	plan = resolve("gibtsnicht", NULL);
	CHECK(has_error(plan, "target not found in AUR: gibtsnicht"));
	aur_plan_free(plan);
	plan = resolve("armonly", NULL);
	CHECK(has_error(plan, "not available for architecture x86_64"));
	aur_plan_free(plan);
	plan = resolve("nosrc", NULL);
	CHECK(has_error(plan, "snapshot missing"));
	aur_plan_free(plan);

	/* .SRCINFO je pkgbase nur einmal holen */
	srcinfo_calls = 0;
	plan = resolve("app", "tgt2");
	CHECK(srcinfo_calls == 6);
	aur_plan_free(plan);

	aur_pkglist_free(rpcpkgs);
	return CHECK_DONE();
}

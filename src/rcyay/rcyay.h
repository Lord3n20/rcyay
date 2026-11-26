/*
 *  rcyay.h - gemeinsamer Zustand der rcyay-Oberfläche
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef RCYAY_H
#define RCYAY_H

#include <alpm.h>
#include <alpm_list.h>

#include "rpc.h"
#include "srcinfo.h"
#include "resolve.h"

#define RCYAY_VERSION "0.1.0"

/* Übersetzungen: rcyay-eigene Texte in der Domäne "rcyay" (src/rcyay/po),
 * pacman-Texte weiter über gettext() aus der Domäne "pacman". */
#if defined(ENABLE_NLS)
#include <libintl.h>
#undef _
#define _(s) dgettext("rcyay", s)
#define P_(s) gettext(s)
#else
#undef _
#define _(s) (s)
#define P_(s) (s)
#endif

typedef struct {
	char *self;               /* Pfad des eigenen Programms */
	char *myname;             /* argv[0] */

	/* Befehlszeile */
	char op;                  /* 'S','G','Y','P','V','h','D','F','Q','R','T','U' oder 0 */
	int help;
	unsigned int cnt[128];    /* Anzahl der Kurzoptionen nach Buchstabe (y, u, s, i …) */
	int noconfirm;
	int needed;
	int asdeps;
	int asexplicit;
	int aur_only;             /* --aur / -a */
	int repo_only;            /* --repo */
	int devel;
	int removemake;           /* --removemake (Standard aus rcyay.conf, ja) */
	int nodiff;
	int nocheck;
	int force_musl;           /* --force-musl */
	alpm_list_t *targets;     /* char* */
	alpm_list_t *tokens;      /* char*: pacman-Optionen (ohne rcyay-eigene, ohne Ziele) */
	alpm_list_t *global;      /* char*: Optionen für jeden pacman-Aufruf (--config …) */
	alpm_list_t *trans;       /* char*: Optionen für Transaktionen (-U/-R/-S) */
	char *configfile, *rootdir, *dbpath, *sysroot, *color;
	alpm_list_t *arches;
	char *rcyayconf;

	/* rcyay.conf */
	char *aururl;
	char *repogiturl;
	char *builddir;
	char *builduser;
	char *sudo;
	char *init;
	char *variant;
	char *overlaydir;         /* Overlay (Standard /usr/share/rcyay/overlay): nur-glibc.txt, dienste/ */
	int pgpfetch;               /* fehlende validpgpkeys importieren (PGPFetch, Standard ja) */
	char *keyserver;          /* PGPKeyserver, NULL = gpg-Standard (wie yay) */
	char *cli_aururl, *cli_builddir;
	int cli_pgpfetch;         /* 0 = aus rcyay.conf, 1 = --pgpfetch, 2 = --nopgpfetch */

	/* Laufzeit */
	aur_ctx_t *ctx;
	alpm_handle_t *handle;
	const char *arch;
	alpm_list_t *aurcache;    /* aur_pkg_t*: RPC-Ergebnisse */
	alpm_list_t *aurmiss;     /* char*: nicht im AUR */
	alpm_list_t *provcache;   /* char*: Namen, für die provides schon gesucht wurde */

	/* Suche */
	char *sortby;             /* votes, popularity, name, modified */
	char *conf_sortby;
	int bottomup;             /* 1 = beste Treffer unten (Standard wie yay) */
} rcyay_t;

extern rcyay_t R;

/* pacman.c (als Bibliothek, main umbenannt) */
int pacman_main(int argc, char *argv[]);

/* args.c */
int rc_parseargs(int argc, char *argv[]);
char **rc_pacman_argv(alpm_list_t *pre, alpm_list_t *tokens, alpm_list_t *targets, int *argc);
alpm_list_t *rc_tokens_without(alpm_list_t *tokens, const char *shortopt, const char *longopt);

/* rconf.c */
int rc_readconf(void);
char *rc_expand_home(const char *path, const char *home);

/* run.c */
int rc_alpm_init(void);
void rc_alpm_release(void);
int rc_alpm_reload(void);
int rc_run(const char *const argv[], const char *dir, uid_t uid, gid_t gid, const char *home, char **out);
int rc_run_quiet(const char *const argv[], const char *dir, uid_t uid, gid_t gid, const char *home, char **out);
void rc_build_path(uid_t uid, gid_t gid, const char *home);
int rc_pacman(alpm_list_t *args, int root);
int rc_pacman_inproc(alpm_list_t *tokens, alpm_list_t *targets);
int rc_build_user(uid_t *uid, gid_t *gid, char **home);
int rc_mkdir_p(const char *dir, mode_t mode);
int rc_chown_r(const char *path, uid_t uid, gid_t gid);
int rc_rm_rf(const char *path);
int rc_have_program(const char *name);
int rc_confirm(int def_yes, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void rc_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rc_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rc_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
alpm_list_t *rc_list_dup(alpm_list_t *l);
alpm_list_t *rc_strlist(const char *first, ...);

/* aur.c */
aur_pkg_t *rc_aur_info(const char *name);
int rc_aur_prefetch(alpm_list_t *names);
alpm_list_t *rc_aur_providers(const char *name);
alpm_pkg_t *rc_repo_find(const char *target);
int rc_is_repo_target(const char *target);
int rc_op_search(void);
int rc_op_info(void);
int rc_op_getpkgbuild(void);
alpm_list_t *rc_aur_upgrades(alpm_list_t **devel);
int rc_is_vcs(const char *name);
int rc_aur_search_list(alpm_list_t *terms, alpm_list_t **out);
void rc_print_aur_search(alpm_list_t *pkgs, int quiet, int numbered, int start);

/* build.c */
int rc_op_sync(void);
int rc_aur_install(alpm_list_t *targets, alpm_list_t *upgrades, alpm_list_t *devel);

/* extra.c */
int rc_op_clean(void);
int rc_op_stats(void);
int rc_op_interactive(void);
void rc_usage(void);

#endif

/*
 *  args.c - Befehlszeile von rcyay (pacman-kompatibel + AUR-Optionen)
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

#include "rcyay.h"

/* pacman-Langoptionen mit Pflichtargument */
static const char *long_arg[] = {
	"dbpath", "root", "sysroot", "config", "ignore", "assume-installed",
	"overwrite", "ask", "cachedir", "hookdir", "logfile", "ignoregroup",
	"arch", "print-format", "gpgdir", "color", NULL
};

/* Optionen, die an jeden pacman-Aufruf gehen */
static const char *long_global[] = {
	"dbpath", "root", "sysroot", "config", "cachedir", "hookdir", "logfile",
	"gpgdir", "color", "arch", "debug", "noprogressbar",
	"disable-download-timeout", "disable-sandbox", NULL
};

/* Optionen für Transaktionen (-S/-U/-R/-D) */
static const char *long_trans[] = {
	"noconfirm", "confirm", "overwrite", "ask", "noscriptlet", "dbonly",
	"nodeps", "assume-installed", "ignore", "ignoregroup", NULL
};

/* Kurzoptionen, die eigene Langnamen haben (für Zählung) */
static const struct { const char *l; char s; } long_short[] = {
	{ "refresh", 'y' }, { "sysupgrade", 'u' }, { "search", 's' }, { "info", 'i' },
	{ "clean", 'c' }, { "groups", 'g' }, { "list", 'l' }, { "print", 'p' },
	{ "downloadonly", 'w' }, { "quiet", 'q' }, { "nodeps", 'd' },
	{ "sync", 'S' }, { "database", 'D' }, { "files", 'F' }, { "query", 'Q' },
	{ "remove", 'R' }, { "deptest", 'T' }, { "upgrade", 'U' }, { "version", 'V' },
	{ "help", 'h' }, { "getpkgbuild", 'G' }, { "yay", 'Y' }, { "show", 'P' },
	{ NULL, 0 }
};

static int in_list(const char **l, const char *s)
{
	for(; *l; l++) {
		if(strcmp(*l, s) == 0) {
			return 1;
		}
	}
	return 0;
}

static int is_op(char c)
{
	return strchr("DFQRSTUVGYP", c) != NULL;
}

static int set_op(char c)
{
	if(R.op && R.op != c) {
		rc_err(_("only one operation may be used at a time\n"));
		return -1;
	}
	R.op = c;
	return 0;
}

static void add_token(const char *t, int global, int trans)
{
	R.tokens = alpm_list_add(R.tokens, strdup(t));
	if(global) {
		R.global = alpm_list_add(R.global, strdup(t));
	}
	if(trans) {
		R.trans = alpm_list_add(R.trans, strdup(t));
	}
}

static void set_path(char **dst, const char *v)
{
	free(*dst);
	*dst = strdup(v);
}

static void note_value(const char *name, const char *v)
{
	if(strcmp(name, "config") == 0) set_path(&R.configfile, v);
	else if(strcmp(name, "root") == 0) set_path(&R.rootdir, v);
	else if(strcmp(name, "dbpath") == 0) set_path(&R.dbpath, v);
	else if(strcmp(name, "sysroot") == 0) set_path(&R.sysroot, v);
	else if(strcmp(name, "color") == 0) set_path(&R.color, v);
	else if(strcmp(name, "arch") == 0) R.arches = alpm_list_add(R.arches, strdup(v));
}

/* Langoption; gibt Anzahl verbrauchter argv-Einträge zurück, -1 bei Fehler */
static int parse_long(int argc, char *argv[], int i)
{
	char *arg = argv[i] + 2, *eq = strchr(arg, '=');
	char name[64];
	const char *val = NULL;
	size_t n = eq ? (size_t)(eq - arg) : strlen(arg);
	int used = 1, glob, trans;

	if(n >= sizeof(name)) {
		rc_err(_("unknown option: %s\n"), argv[i]);
		return -1;
	}
	memcpy(name, arg, n);
	name[n] = '\0';
	if(eq) {
		val = eq + 1;
	}

	/* rcyay-eigene Optionen */
	if(strcmp(name, "aur") == 0) { R.aur_only = 1; return 1; }
	if(strcmp(name, "repo") == 0) { R.repo_only = 1; return 1; }
	if(strcmp(name, "devel") == 0) { R.devel = 1; return 1; }
	if(strcmp(name, "nodevel") == 0) { R.devel = 0; return 1; }
	if(strcmp(name, "removemake") == 0) { R.removemake = 1; return 1; }
	if(strcmp(name, "noremovemake") == 0 || strcmp(name, "keepmake") == 0) {
		R.removemake = 0;
		return 1;
	}
	if(strcmp(name, "nodiff") == 0) { R.nodiff = 1; return 1; }
	if(strcmp(name, "pgpfetch") == 0) { R.cli_pgpfetch = 1; return 1; }
	if(strcmp(name, "nopgpfetch") == 0) { R.cli_pgpfetch = 2; return 1; }
	if(strcmp(name, "topdown") == 0) { R.bottomup = 0; return 1; }
	if(strcmp(name, "bottomup") == 0) { R.bottomup = 1; return 1; }
	if(strcmp(name, "nocheck") == 0) { R.nocheck = 1; return 1; }
	if(strcmp(name, "force-musl") == 0) { R.force_musl = 1; return 1; }
	if(strcmp(name, "provides") == 0) {
		R.provcmd = "list";
		if(val) {
			R.provcmd = val;
		} else if(i + 1 < argc && (strcmp(argv[i + 1], "list") == 0 || strcmp(argv[i + 1], "apply") == 0)) {
			R.provcmd = argv[i + 1];
			used = 2;
		}
		return used;
	}
	if(strcmp(name, "enable") == 0) { R.svcmd = "enable"; return 1; }
	if(strcmp(name, "disable") == 0) { R.svcmd = "disable"; return 1; }
	if(strcmp(name, "services") == 0 || strcmp(name, "dienste") == 0 || strcmp(name, "init") == 0) {
		static char sub[32];
		if(!val) {
			if(i + 1 >= argc) {
				rc_err(_("option --%s requires an argument\n"), name);
				return -1;
			}
			val = argv[i + 1];
			used = 2;
		}
		if(strcmp(name, "init") == 0) {
			R.svcmd = "init";
			R.targets = alpm_list_add(R.targets, strdup(val));
		} else {
			snprintf(sub, sizeof(sub), "%s", val);
			R.svcmd = sub;
		}
		return used;
	}
	if(strcmp(name, "aururl") == 0 || strcmp(name, "builddir") == 0 ||
			strcmp(name, "rcyayconf") == 0 || strcmp(name, "sortby") == 0) {
		if(!val) {
			if(i + 1 >= argc) {
				rc_err(_("option --%s requires an argument\n"), name);
				return -1;
			}
			val = argv[i + 1];
			used = 2;
		}
		if(name[0] == 's') {
			if(strcmp(val, "votes") && strcmp(val, "popularity") && strcmp(val, "name") &&
					strcmp(val, "modified")) {
				rc_err(_("invalid argument '%s' for %s\n"), val, "--sortby");
				return -1;
			}
			set_path(&R.sortby, val);
		} else if(name[0] == 'a') set_path(&R.cli_aururl, val);
		else if(name[0] == 'b') set_path(&R.cli_builddir, val);
		else set_path(&R.rcyayconf, val);
		return used;
	}

	for(int k = 0; long_short[k].l; k++) {
		if(strcmp(name, long_short[k].l) == 0) {
			char c = long_short[k].s;
			if(is_op(c)) {
				if(set_op(c) != 0) return -1;
			} else if(c == 'h') {
				R.help = 1;
			} else {
				R.cnt[(unsigned char)c]++;
			}
			if(c == 'G' || c == 'Y' || c == 'P') {
				return 1; /* nicht an pacman */
			}
			break;
		}
	}
	if(strcmp(name, "noconfirm") == 0) R.noconfirm = 1;
	if(strcmp(name, "confirm") == 0) R.noconfirm = 0;
	if(strcmp(name, "needed") == 0) R.needed = 1;
	if(strcmp(name, "asdeps") == 0) R.asdeps = 1;
	if(strcmp(name, "asexplicit") == 0) R.asexplicit = 1;

	glob = in_list(long_global, name);
	trans = in_list(long_trans, name);
	if(in_list(long_arg, name) && !val) {
		if(i + 1 >= argc) {
			rc_err(_("option --%s requires an argument\n"), name);
			return -1;
		}
		val = argv[i + 1];
		used = 2;
		add_token(argv[i], glob, trans);
		add_token(argv[i + 1], glob, trans);
	} else {
		add_token(argv[i], glob, trans);
	}
	if(val) {
		note_value(name, val);
	}
	return used;
}

static int parse_short(int argc, char *argv[], int i)
{
	const char *s = argv[i] + 1;
	char tok[3] = { '-', 0, 0 };
	for(; *s; s++) {
		char c = *s;
		if(c == 'b' || c == 'r') {
			const char *v = s[1] ? s + 1 : NULL;
			int used = 1;
			if(!v) {
				if(i + 1 >= argc) {
					rc_err(_("option -%c requires an argument\n"), c);
					return -1;
				}
				v = argv[i + 1];
				used = 2;
			}
			tok[1] = c;
			add_token(tok, 1, 0);
			add_token(v, 1, 0);
			note_value(c == 'b' ? "dbpath" : "root", v);
			return used;
		}
		if(c == 'a') {
			R.aur_only = 1;
			continue;
		}
		if(c == 'h') {
			R.help = 1;
		} else if(is_op(c)) {
			if(set_op(c) != 0) {
				return -1;
			}
		} else {
			R.cnt[(unsigned char)c]++;
		}
		if(c == 'G' || c == 'Y' || c == 'P') {
			continue;
		}
		tok[1] = c;
		add_token(tok, 0, c == 'd');
	}
	return 1;
}

int rc_parseargs(int argc, char *argv[])
{
	int i = 1;
	while(i < argc) {
		const char *a = argv[i];
		int used;
		if(strcmp(a, "--") == 0) {
			for(i++; i < argc; i++) {
				R.targets = alpm_list_add(R.targets, strdup(argv[i]));
			}
			break;
		}
		if(a[0] == '-' && a[1] == '-') {
			used = parse_long(argc, argv, i);
		} else if(a[0] == '-' && a[1] != '\0') {
			used = parse_short(argc, argv, i);
		} else {
			R.targets = alpm_list_add(R.targets, strdup(a));
			used = 1;
		}
		if(used < 0) {
			return -1;
		}
		i += used;
	}
	return 0;
}

/* Baut argv für pacman_main/den pacman-Kindprozess:
 * argv0 pre… tokens… [--] targets… */
char **rc_pacman_argv(alpm_list_t *pre, alpm_list_t *tokens, alpm_list_t *targets, int *argc)
{
	size_t n = 1 + alpm_list_count(pre) + alpm_list_count(tokens) + alpm_list_count(targets) + 2;
	char **v = calloc(n, sizeof(char *));
	int k = 0, dash = 0;
	if(!v) {
		return NULL;
	}
	v[k++] = R.myname;
	for(alpm_list_t *i = pre; i; i = i->next) v[k++] = i->data;
	for(alpm_list_t *i = tokens; i; i = i->next) v[k++] = i->data;
	for(alpm_list_t *i = targets; i; i = i->next) {
		const char *t = i->data;
		if(t[0] == '-' && t[1] != '\0') dash = 1;
	}
	if(dash) v[k++] = (char *)"--";
	for(alpm_list_t *i = targets; i; i = i->next) v[k++] = i->data;
	v[k] = NULL;
	if(argc) {
		*argc = k;
	}
	return v;
}

/* Kopie der Token-Liste ohne die angegebene Option (Kurz- und Langform). */
alpm_list_t *rc_tokens_without(alpm_list_t *tokens, const char *shortopt, const char *longopt)
{
	alpm_list_t *r = NULL;
	for(alpm_list_t *i = tokens; i; i = i->next) {
		const char *t = i->data;
		if((shortopt && strcmp(t, shortopt) == 0) || (longopt && strcmp(t, longopt) == 0)) {
			continue;
		}
		r = alpm_list_add(r, (void *)t);
	}
	return r;
}

/*
 *  unit.c - systemd-Units lesen und in runit/s6/BusyBox-init-Dienste übersetzen
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Übersetzt werden die einfachen, häufigen Fälle (PLAN.md 1.3):
 *  Type=simple|exec|notify|notify-reload|idle|dbus|forking (mit PIDFile)|oneshot,
 *  ExecStartPre/ExecStart/ExecStop (forking), User/Group, Environment,
 *  EnvironmentFile, WorkingDirectory, UMask, LimitNOFILE, Runtime-/State-/Cache-/
 *  LogsDirectory, ConditionPath*, Requires/BindsTo (Warten auf andere Dienste).
 *  Abschottung (Protect*, Private*, SystemCall* …) entfällt ohne systemd.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "unit.h"

#define FREELIST_U(l) do { alpm_list_free_inner(l, free); alpm_list_free(l); l = NULL; } while(0)

/* ---------- kleiner Puffer ---------- */

typedef struct {
	char *s;
	size_t len, cap;
	int oom;
} sb_t;

static void sb_add(sb_t *b, const char *s, size_t n)
{
	if(b->oom) return;
	if(b->len + n + 1 > b->cap) {
		size_t nc = b->cap ? b->cap : 256;
		char *ns;
		while(nc < b->len + n + 1) nc *= 2;
		ns = realloc(b->s, nc);
		if(!ns) {
			b->oom = 1;
			return;
		}
		b->s = ns;
		b->cap = nc;
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = '\0';
}

static void sb_puts(sb_t *b, const char *s)
{
	sb_add(b, s, strlen(s));
}

__attribute__((format(printf, 2, 3)))
static void sb_printf(sb_t *b, const char *fmt, ...)
{
	char *t = NULL;
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vasprintf(&t, fmt, ap);
	va_end(ap);
	if(n < 0) {
		b->oom = 1;
		return;
	}
	sb_add(b, t, (size_t)n);
	free(t);
}

static char *sb_take(sb_t *b)
{
	char *r;
	if(b->oom) {
		free(b->s);
		return NULL;
	}
	r = b->s ? b->s : strdup("");
	b->s = NULL;
	b->len = b->cap = 0;
	return r;
}

__attribute__((format(printf, 1, 2)))
static char *fmt(const char *f, ...)
{
	char *t = NULL;
	va_list ap;
	va_start(ap, f);
	if(vasprintf(&t, f, ap) < 0) t = NULL;
	va_end(ap);
	return t;
}

/* ---------- Init-Namen ---------- */

rc_init_t rc_init_parse(const char *s)
{
	if(!s || !*s) return RC_INIT_NONE;
	if(!strcmp(s, "systemd")) return RC_INIT_SYSTEMD;
	if(!strcmp(s, "busybox")) return RC_INIT_BUSYBOX;
	if(!strcmp(s, "runit")) return RC_INIT_RUNIT;
	if(!strcmp(s, "s6")) return RC_INIT_S6;
	return (rc_init_t)-1;
}

const char *rc_init_name(rc_init_t init)
{
	switch(init) {
		case RC_INIT_SYSTEMD: return "systemd";
		case RC_INIT_BUSYBOX: return "busybox";
		case RC_INIT_RUNIT: return "runit";
		case RC_INIT_S6: return "s6";
		default: return "systemd";
	}
}

/* ---------- Parser ---------- */

static char *trim(char *s)
{
	char *e;
	while(*s == ' ' || *s == '\t') s++;
	e = s + strlen(s);
	while(e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	*e = '\0';
	return s;
}

void rc_unit_free(rc_unit_t *u)
{
	if(!u) return;
	for(alpm_list_t *i = u->kv; i; i = i->next) {
		rc_unit_kv_t *kv = i->data;
		free(kv->section);
		free(kv->key);
		free(kv->value);
		free(kv);
	}
	alpm_list_free(u->kv);
	free(u->name);
	free(u->file);
	free(u);
}

rc_unit_t *rc_unit_parse(const char *filename, const char *text, char **err)
{
	rc_unit_t *u = calloc(1, sizeof(*u));
	const char *base = strrchr(filename, '/');
	char *section = NULL, *buf = NULL;
	const char *p = text;
	size_t blen = 0;
	int lineno = 0;

	*err = NULL;
	if(!u) return NULL;
	base = base ? base + 1 : filename;
	u->file = strdup(filename);
	if(strlen(base) <= 8 || strcmp(base + strlen(base) - 8, ".service") != 0) {
		*err = fmt("%s: not a .service unit", base);
		goto fail;
	}
	u->name = strndup(base, strlen(base) - 8);
	if(!u->name || !u->file) goto oom;
	if(u->name[0] && u->name[strlen(u->name) - 1] == '@') {
		u->template = 1;
	}
	if(strstr(filename, "/systemd/user/")) {
		u->user = 1;
	}

	while(*p) {
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		char *line, *nb;
		int cont;
		lineno++;
		/* Fortsetzungszeilen (\ am Ende) sammeln */
		nb = realloc(buf, blen + n + 2);
		if(!nb) goto oom;
		buf = nb;
		memcpy(buf + blen, p, n);
		blen += n;
		buf[blen] = '\0';
		p = nl ? nl + 1 : p + n;
		while(blen && (buf[blen - 1] == '\r' || buf[blen - 1] == ' ' || buf[blen - 1] == '\t')) buf[--blen] = '\0';
		cont = blen && buf[blen - 1] == '\\';
		if(cont && *p) {
			buf[blen - 1] = ' ';
			continue;
		}
		if(cont) buf[--blen] = '\0';
		line = trim(buf);
		if(!*line || *line == '#' || *line == ';') {
			blen = 0;
			continue;
		}
		if(*line == '[') {
			char *e = strchr(line, ']');
			if(!e) {
				*err = fmt("%s:%d: broken section header", base, lineno);
				goto fail;
			}
			*e = '\0';
			free(section);
			section = strdup(line + 1);
			if(!section) goto oom;
		} else {
			char *eq = strchr(line, '=');
			rc_unit_kv_t *kv;
			if(!eq) {
				*err = fmt("%s:%d: expected KEY=VALUE", base, lineno);
				goto fail;
			}
			if(!section) {
				*err = fmt("%s:%d: assignment outside a section", base, lineno);
				goto fail;
			}
			*eq = '\0';
			kv = calloc(1, sizeof(*kv));
			if(!kv) goto oom;
			kv->section = strdup(section);
			kv->key = strdup(trim(line));
			kv->value = strdup(trim(eq + 1));
			u->kv = alpm_list_add(u->kv, kv);
			if(!kv->section || !kv->key || !kv->value) goto oom;
		}
		blen = 0;
	}
	free(buf);
	free(section);
	return u;
oom:
	if(!*err) *err = strdup("out of memory");
fail:
	free(buf);
	free(section);
	rc_unit_free(u);
	return NULL;
}

const char *rc_unit_get(rc_unit_t *u, const char *section, const char *key)
{
	const char *v = NULL;
	for(alpm_list_t *i = u->kv; i; i = i->next) {
		rc_unit_kv_t *kv = i->data;
		if(!strcmp(kv->section, section) && !strcmp(kv->key, key)) {
			v = kv->value;
		}
	}
	return v && *v ? v : NULL;
}

alpm_list_t *rc_unit_all(rc_unit_t *u, const char *section, const char *key)
{
	alpm_list_t *r = NULL;
	for(alpm_list_t *i = u->kv; i; i = i->next) {
		rc_unit_kv_t *kv = i->data;
		if(!strcmp(kv->section, section) && !strcmp(kv->key, key)) {
			if(!*kv->value) {
				alpm_list_free(r);
				r = NULL;
			} else {
				r = alpm_list_add(r, kv->value);
			}
		}
	}
	return r;
}

static int has_section(rc_unit_t *u, const char *section)
{
	for(alpm_list_t *i = u->kv; i; i = i->next) {
		if(!strcmp(((rc_unit_kv_t *)i->data)->section, section)) return 1;
	}
	return 0;
}

/* ---------- Übersetzung ---------- */

typedef struct {
	rc_unit_t *u;
	rc_init_t init;
	const char *user;
	char *why;
	alpm_list_t **notes;
} tr_t;

static void note(tr_t *t, char *msg)
{
	if(msg) *t->notes = alpm_list_add(*t->notes, msg);
}

static int fail(tr_t *t, char *why)
{
	if(!t->why) t->why = why;
	else free(why);
	return -1;
}

/* systemd-Spezifizierer (%n, %i, %t …); NULL bei unbekanntem Spezifizierer */
static char *specifiers(tr_t *t, const char *v)
{
	sb_t b = { 0 };
	for(const char *p = v; *p; p++) {
		if(*p != '%') {
			sb_add(&b, p, 1);
			continue;
		}
		p++;
		switch(*p) {
			case '%': sb_puts(&b, "%"); break;
			case 'n': sb_printf(&b, "%s.service", t->u->name); break;
			case 'N': case 'p': case 'P': {
				size_t n = strcspn(t->u->name, "@");
				sb_add(&b, t->u->name, n);
				break;
			}
			case 'i': case 'I': case 'j': case 'J': break;  /* keine Instanz */
			case 't': sb_puts(&b, "/run"); break;
			case 'S': sb_puts(&b, "/var/lib"); break;
			case 'C': sb_puts(&b, "/var/cache"); break;
			case 'L': sb_puts(&b, "/var/log"); break;
			case 'E': sb_puts(&b, "/etc"); break;
			case 'T': sb_puts(&b, "/tmp"); break;
			case 'V': sb_puts(&b, "/var/tmp"); break;
			case 'u': sb_puts(&b, t->user ? t->user : "root"); break;
			case 'h': sb_puts(&b, t->user ? "~" : "/root"); break;
			default:
				free(b.s);
				fail(t, fmt("unsupported specifier %%%c", *p ? *p : '?'));
				return NULL;
		}
		if(!*p) break;
	}
	return sb_take(&b);
}

/* Wörter nach systemd-Regeln (Anführungszeichen, \-Escapes) */
static alpm_list_t *split_words(const char *s, int *bad)
{
	alpm_list_t *words = NULL;
	const char *p = s;
	*bad = 0;
	for(;;) {
		sb_t w = { 0 };
		int any = 0;
		while(*p == ' ' || *p == '\t') p++;
		if(!*p) break;
		while(*p && *p != ' ' && *p != '\t') {
			if(*p == '"' || *p == '\'') {
				char q = *p++;
				any = 1;
				while(*p && *p != q) {
					if(*p == '\\' && p[1] && q == '"') {
						p++;
						switch(*p) {
							case 'n': sb_puts(&w, "\n"); break;
							case 't': sb_puts(&w, "\t"); break;
							default: sb_add(&w, p, 1);
						}
						p++;
					} else {
						sb_add(&w, p++, 1);
					}
				}
				if(*p != q) {
					*bad = 1;
				} else {
					p++;
				}
			} else if(*p == '\\' && p[1]) {
				p++;
				switch(*p) {
					case 'n': sb_puts(&w, "\n"); break;
					case 't': sb_puts(&w, "\t"); break;
					default: sb_add(&w, p, 1);
				}
				p++;
				any = 1;
			} else {
				sb_add(&w, p++, 1);
				any = 1;
			}
		}
		if(any) {
			char *x = sb_take(&w);
			if(!x) {
				*bad = 1;
				break;
			}
			words = alpm_list_add(words, x);
		}
	}
	return words;
}

static int is_varname_start(char c)
{
	return c == '_' || isalpha((unsigned char)c);
}

static int is_varname(char c)
{
	return c == '_' || isalnum((unsigned char)c);
}

/* Ein Wort shell-sicher ausgeben; expand: $VAR/${VAR} wie systemd auflösen
 * ($VAR allein = in Wörter zerlegt, ${VAR} = ein Wort, $$ = $) */
static void emit_word(sb_t *b, const char *w, int expand)
{
	int hasvar = 0;
	if(expand) {
		for(const char *p = w; *p; p++) {
			if(*p == '$') {
				hasvar = 1;
				break;
			}
		}
	}
	if(hasvar && w[0] == '$' && is_varname_start(w[1])) {
		const char *p = w + 1;
		while(is_varname(*p)) p++;
		if(!*p) {
			sb_puts(b, w);  /* $VAR allein: Wortzerlegung wie systemd */
			return;
		}
	}
	if(hasvar) {
		sb_puts(b, "\"");
		for(const char *p = w; *p; p++) {
			if(*p == '$' && p[1] == '$') {
				sb_puts(b, "\\$");
				p++;
			} else if(*p == '$' && p[1] == '{' && is_varname_start(p[2])) {
				const char *e = p + 2;
				while(is_varname(*e)) e++;
				if(*e == '}') {
					sb_add(b, p, (size_t)(e - p + 1));
					p = e;
				} else {
					sb_puts(b, "\\$");
				}
			} else if(*p == '$' && is_varname_start(p[1])) {
				const char *e = p + 1;
				while(is_varname(*e)) e++;
				sb_puts(b, "${");
				sb_add(b, p + 1, (size_t)(e - p - 1));
				sb_puts(b, "}");
				p = e - 1;
			} else if(*p == '$' || *p == '"' || *p == '\\' || *p == '`') {
				sb_puts(b, "\\");
				sb_add(b, p, 1);
			} else {
				sb_add(b, p, 1);
			}
		}
		sb_puts(b, "\"");
		return;
	}
	/* einfache Wörter ohne Sonderzeichen unverändert */
	if(*w && strspn(w, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-+=/.,:@%") == strlen(w)) {
		sb_puts(b, w);
		return;
	}
	sb_puts(b, "'");
	for(const char *p = w; *p; p++) {
		if(*p == '\'') sb_puts(b, "'\\''");
		else sb_add(b, p, 1);
	}
	sb_puts(b, "'");
}

static void emit_quoted(sb_t *b, const char *s)
{
	emit_word(b, s, 0);
}

typedef struct {
	int ignore_fail;   /* - */
	int as_root;       /* + ! !! : ohne User= */
	int noexpand;      /* : */
	char *cmd;         /* fertige Shell-Befehlszeile (ohne exec/chpst) */
} cmd_t;

/* Exec*=-Zeile → Shell-Befehl. 0 = ok, -1 = nicht übersetzbar */
static int exec_line(tr_t *t, const char *raw, cmd_t *c)
{
	const char *p = raw;
	char *v;
	alpm_list_t *words;
	int bad;
	sb_t b = { 0 };

	memset(c, 0, sizeof(*c));
	for(;; p++) {
		if(*p == '-') c->ignore_fail = 1;
		else if(*p == '+' || *p == '!') c->as_root = 1;
		else if(*p == ':') c->noexpand = 1;
		else if(*p == '@') return fail(t, fmt("ExecStart prefix '@' (argv[0] override)"));
		else if(*p == '|') return fail(t, fmt("ExecStart prefix '|'"));
		else break;
	}
	v = specifiers(t, p);
	if(!v) return -1;
	words = split_words(v, &bad);
	free(v);
	if(bad || !words) {
		FREELIST_U(words);
		return fail(t, fmt("cannot parse command line '%s'", raw));
	}
	for(alpm_list_t *i = words; i; i = i->next) {
		if(!strcmp(i->data, ";")) {
			FREELIST_U(words);
			return fail(t, fmt("several commands in one line (';')"));
		}
		if(i != words) sb_puts(&b, " ");
		emit_word(&b, i->data, !c->noexpand);
	}
	FREELIST_U(words);
	c->cmd = sb_take(&b);
	return c->cmd ? 0 : -1;
}

static void emit_user(tr_t *t, sb_t *b, const char *user, const char *group)
{
	(void)t;
	/* chpst gibt es mit runit und als BusyBox-Applet (UsrsideOS hat immer
	 * BusyBox) – ein Weg für alle drei Inits */
	if(group) sb_printf(b, "chpst -u %s:%s ", user, group);
	else sb_printf(b, "chpst -u %s ", user);
}

/* Bedingungen ConditionPath*: 1 = ok, Text "Bedingung" in *expr */
static int conditions(tr_t *t, char **expr)
{
	static const struct { const char *key, *test; } C[] = {
		{ "ConditionPathExists", "-e" }, { "ConditionPathIsDirectory", "-d" },
		{ "ConditionFileNotEmpty", "-s" }, { "ConditionFileIsExecutable", "-x" },
		{ "ConditionPathIsSymbolicLink", "-L" },
		{ "AssertPathExists", "-e" }, { "AssertPathIsDirectory", "-d" },
		{ "AssertFileNotEmpty", "-s" }, { "AssertFileIsExecutable", "-x" },
		{ NULL, NULL }
	};
	sb_t all = { 0 }, any = { 0 };
	int nall = 0, nany = 0;

	for(alpm_list_t *i = t->u->kv; i; i = i->next) {
		rc_unit_kv_t *kv = i->data;
		const char *v = kv->value, *test = NULL;
		int neg = 0, trig = 0;
		char *path;
		if(strcmp(kv->section, "Unit") != 0) continue;
		if(strncmp(kv->key, "Condition", 9) != 0 && strncmp(kv->key, "Assert", 6) != 0) continue;
		for(int k = 0; C[k].key; k++) {
			if(!strcmp(kv->key, C[k].key)) test = C[k].test;
		}
		if(!test) {
			if(strcmp(kv->key, "ConditionUser") != 0) {
				note(t, fmt("%s: %s=%s ignored", t->u->name, kv->key, v));
			}
			continue;
		}
		if(!*v) continue;
		if(*v == '|') { trig = 1; v++; }
		if(*v == '!') { neg = 1; v++; }
		path = specifiers(t, v);
		if(!path) {
			free(all.s);
			free(any.s);
			return -1;
		}
		{
			sb_t *b = trig ? &any : &all;
			int *n = trig ? &nany : &nall;
			if(*n) sb_puts(b, trig ? " || " : " && ");
			sb_printf(b, "%s[ %s ", neg ? "! " : "", test);
			emit_quoted(b, path);
			sb_puts(b, " ]");
			(*n)++;
		}
		free(path);
	}
	if(!nall && !nany) {
		*expr = NULL;
		free(all.s);
		free(any.s);
		return 0;
	}
	{
		sb_t e = { 0 };
		char *a = sb_take(&all), *o = sb_take(&any);
		if(nall) sb_puts(&e, a);
		if(nall && nany) sb_puts(&e, " && ");
		if(nany) sb_printf(&e, nall ? "{ %s; }" : "%s", o);
		free(a);
		free(o);
		*expr = sb_take(&e);
	}
	return *expr ? 1 : -1;
}

/* gemeinsamer Anfang von run/start */
static int prelude(tr_t *t, sb_t *b, const char *what, alpm_list_t **needs, int oneshot)
{
	const char *user = rc_unit_get(t->u, "Service", "User");
	const char *group = rc_unit_get(t->u, "Service", "Group");
	const char *umask_ = rc_unit_get(t->u, "Service", "UMask");
	const char *nofile = rc_unit_get(t->u, "Service", "LimitNOFILE");
	const char *wd = rc_unit_get(t->u, "Service", "WorkingDirectory");
	const char *desc = rc_unit_get(t->u, "Unit", "Description");
	char *cond = NULL;
	alpm_list_t *l;
	static const struct { const char *key, *base; } DIRS[] = {
		{ "RuntimeDirectory", "/run" }, { "StateDirectory", "/var/lib" },
		{ "CacheDirectory", "/var/cache" }, { "LogsDirectory", "/var/log" },
		{ "ConfigurationDirectory", "/etc" }, { NULL, NULL }
	};

	sb_printf(b, "#!/bin/sh\n# rcyay-dienste: %s, generated from %s for %s\n",
			what, t->u->file, rc_init_name(t->init));
	if(desc) sb_printf(b, "# %s\n", desc);
	sb_puts(b, "# Do not edit: regenerated on every update. Own version: overlay/dienste/");
	sb_printf(b, "%s/ or /etc/sv/%s/\n", t->u->name, t->u->name);
	sb_puts(b, "PATH=/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin\nexport PATH\nexec 2>&1\n");

	if(conditions(t, &cond) < 0) return -1;
	if(cond) {
		if(oneshot) {
			sb_printf(b, "%s || exit 0\n", cond);
		} else {
			/* Bedingung nicht erfüllt: Dienst bleibt still, statt dauernd neu zu starten */
			sb_printf(b, "if ! { %s; }; then\n\techo \"%s: condition not met, idle\"\n\texec sleep 2147483647\nfi\n",
					cond, t->u->name);
		}
		free(cond);
	}

	/* Abhängigkeiten: auf laufende Dienste warten (Supervisor startet neu) */
	if(needs && t->init != RC_INIT_BUSYBOX) {
		alpm_list_t *deps = NULL;
		const char *keys[] = { "Requires", "BindsTo", "Requisite", NULL };
		for(int k = 0; keys[k]; k++) {
			alpm_list_t *vals = rc_unit_all(t->u, "Unit", keys[k]);
			for(alpm_list_t *i = vals; i; i = i->next) {
				int bad;
				alpm_list_t *ws = split_words(i->data, &bad);
				for(alpm_list_t *w = ws; w; w = w->next) {
					char *d = w->data;
					size_t n = strlen(d);
					if(n > 8 && !strcmp(d + n - 8, ".service") && !strchr(d, '@')) {
						d[n - 8] = '\0';
						if(!alpm_list_find_str(deps, d)) deps = alpm_list_add(deps, strdup(d));
					}
				}
				FREELIST_U(ws);
			}
			alpm_list_free(vals);
		}
		for(alpm_list_t *i = deps; i; i = i->next) {
			const char *d = i->data;
			if(t->init == RC_INIT_RUNIT) {
				sb_printf(b, "if [ -e /var/service/%s ]; then sv check %s >/dev/null || exit 1; fi\n", d, d);
			} else if(t->init == RC_INIT_S6) {
				sb_printf(b, "if [ -e /run/service/%s ]; then s6-svwait -u -t 10000 /run/service/%s || exit 1; fi\n", d, d);
			}
		}
		*needs = deps;
	}

	if(umask_) {
		if(strspn(umask_, "01234567") != strlen(umask_)) return fail(t, fmt("invalid UMask=%s", umask_));
		sb_printf(b, "umask %s\n", umask_);
	}
	if(nofile) {
		char *c = strdup(nofile), *colon;
		if(!c) return -1;
		if((colon = strchr(c, ':'))) *colon = '\0';
		if(!strcmp(c, "infinity")) sb_puts(b, "ulimit -n unlimited 2>/dev/null\n");
		else if(strspn(c, "0123456789") == strlen(c) && *c) sb_printf(b, "ulimit -n %s 2>/dev/null\n", c);
		else note(t, fmt("%s: LimitNOFILE=%s ignored", t->u->name, nofile));
		free(c);
	}
	for(int k = 0; DIRS[k].key; k++) {
		alpm_list_t *vals = rc_unit_all(t->u, "Service", DIRS[k].key);
		for(alpm_list_t *i = vals; i; i = i->next) {
			int bad;
			alpm_list_t *ws = split_words(i->data, &bad);
			for(alpm_list_t *w = ws; w; w = w->next) {
				char *d = w->data, *c;
				if((c = strchr(d, ':'))) *c = '\0';  /* QUELLE:ZIEL – nur die Quelle */
				if(d[0] == '/' || strstr(d, "..")) {
					FREELIST_U(ws);
					alpm_list_free(vals);
					return fail(t, fmt("invalid %s=%s", DIRS[k].key, (char *)i->data));
				}
				sb_printf(b, "mkdir -p %s/", DIRS[k].base);
				emit_quoted(b, d);
				sb_puts(b, "\n");
				if(user && strcmp(DIRS[k].base, "/etc") != 0) {
					sb_printf(b, "chown %s%s%s %s/", user, group ? ":" : "", group ? group : "", DIRS[k].base);
					emit_quoted(b, d);
					sb_puts(b, "\n");
				}
			}
			FREELIST_U(ws);
		}
		alpm_list_free(vals);
	}

	l = rc_unit_all(t->u, "Service", "Environment");
	for(alpm_list_t *i = l; i; i = i->next) {
		char *v = specifiers(t, i->data);
		int bad;
		alpm_list_t *ws = v ? split_words(v, &bad) : NULL;
		free(v);
		for(alpm_list_t *w = ws; w; w = w->next) {
			char *a = w->data, *eq = strchr(a, '=');
			if(!eq || eq == a) continue;
			*eq = '\0';
			if(strspn(a, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(a)) continue;
			sb_printf(b, "export %s=", a);
			emit_quoted(b, eq + 1);
			sb_puts(b, "\n");
		}
		FREELIST_U(ws);
	}
	alpm_list_free(l);
	l = rc_unit_all(t->u, "Service", "EnvironmentFile");
	for(alpm_list_t *i = l; i; i = i->next) {
		const char *v = i->data;
		int opt = *v == '-';
		char *path = specifiers(t, opt ? v + 1 : v);
		if(!path) {
			alpm_list_free(l);
			return -1;
		}
		/* KEY=VALUE-Dateien; als Shell-Zuweisungen gelesen (wie bei Artix/Void üblich) */
		if(opt) {
			sb_puts(b, "if [ -r ");
			emit_quoted(b, path);
			sb_puts(b, " ]; then set -a; . ");
			emit_quoted(b, path);
			sb_puts(b, "; set +a; fi\n");
		} else {
			sb_puts(b, "set -a; . ");
			emit_quoted(b, path);
			sb_puts(b, " || exit 1; set +a\n");
		}
		free(path);
	}
	alpm_list_free(l);
	if(wd) {
		int opt = *wd == '-';
		char *path = specifiers(t, opt ? wd + 1 : wd);
		if(!path) return -1;
		if(!strcmp(path, "~")) {
			free(path);
			path = strdup(user ? "~" : "/root");
		}
		if(strcmp(path, "~") == 0 && user) {
			sb_printf(b, "cd ~%s%s\n", user, opt ? " 2>/dev/null" : " || exit 1");
		} else {
			sb_puts(b, "cd ");
			emit_quoted(b, path);
			sb_puts(b, opt ? " 2>/dev/null\n" : " || exit 1\n");
		}
		free(path);
	}
	(void)group;
	return 0;
}

static int emit_cmds(tr_t *t, sb_t *b, const char *key, int last_exec)
{
	const char *user = rc_unit_get(t->u, "Service", "User");
	const char *group = rc_unit_get(t->u, "Service", "Group");
	alpm_list_t *l = rc_unit_all(t->u, "Service", key);
	int ret = 0;
	if(user && !strcmp(user, "root")) user = NULL;
	for(alpm_list_t *i = l; i; i = i->next) {
		cmd_t c;
		int isexec = last_exec && !i->next;
		if(exec_line(t, i->data, &c) != 0) {
			ret = -1;
			break;
		}
		if(isexec && !c.ignore_fail) sb_puts(b, "exec ");
		if(user && !c.as_root) emit_user(t, b, user, group);
		sb_puts(b, c.cmd);
		if(!isexec || c.ignore_fail) {
			sb_puts(b, c.ignore_fail ? " || true\n" : " || exit $?\n");
		} else {
			sb_puts(b, "\n");
		}
		free(c.cmd);
	}
	alpm_list_free(l);
	return ret;
}

void rc_svout_free(rc_svout_t *o)
{
	if(!o) return;
	free(o->run);
	free(o->finish);
	free(o->start);
	FREELIST_U(o->needs);
	memset(o, 0, sizeof(*o));
}

int rc_unit_translate(rc_unit_t *u, rc_init_t init, rc_svout_t *out, char **why, alpm_list_t **notes)
{
	tr_t t = { u, init, NULL, NULL, notes };
	const char *type = rc_unit_get(u, "Service", "Type");
	alpm_list_t *starts;
	int nstart;
	sb_t b = { 0 };

	memset(out, 0, sizeof(*out));
	*why = NULL;
	t.user = rc_unit_get(u, "Service", "User");
	if(u->user) {
		*why = fmt("%s: user unit - start it from your session (e.g. niri spawn-at-startup), not translated", u->name);
		return 1;
	}
	if(u->template) {
		*why = fmt("%s: template unit - not translated (own service: overlay/dienste/%s)", u->name, u->name);
		return 1;
	}
	if(!has_section(u, "Install") || (!rc_unit_get(u, "Install", "WantedBy") &&
				!rc_unit_get(u, "Install", "RequiredBy") && !rc_unit_get(u, "Install", "UpheldBy"))) {
		*why = fmt("%s: static unit (no [Install]) - started by other units, not translated", u->name);
		return 1;
	}
	if(!type) type = rc_unit_get(u, "Service", "BusName") ? "dbus" : "simple";
	starts = rc_unit_all(u, "Service", "ExecStart");
	nstart = (int)alpm_list_count(starts);
	alpm_list_free(starts);
	if(rc_unit_get(u, "Service", "RootDirectory") || rc_unit_get(u, "Service", "RootImage")) {
		fail(&t, fmt("RootDirectory/RootImage"));
		goto bad;
	}

	if(!strcmp(type, "oneshot")) {
		out->kind = RC_SV_ONESHOT;
		if(nstart == 0) {
			fail(&t, fmt("oneshot without ExecStart"));
			goto bad;
		}
		if(prelude(&t, &b, "oneshot", NULL, 1) != 0 ||
				emit_cmds(&t, &b, "ExecStartPre", 0) != 0 ||
				emit_cmds(&t, &b, "ExecStart", 0) != 0 ||
				emit_cmds(&t, &b, "ExecStartPost", 0) != 0) {
			goto bad;
		}
		sb_puts(&b, "exit 0\n");
		out->start = sb_take(&b);
		*why = NULL;
		return out->start ? 0 : -1;
	}

	if(nstart != 1) {
		fail(&t, fmt(nstart ? "several ExecStart= lines (only allowed for oneshot)" : "no ExecStart="));
		goto bad;
	}
	if(!strcmp(type, "dbus")) {
		note(&t, fmt("%s: needs a running D-Bus system bus (dbus service)", u->name));
	} else if(strcmp(type, "simple") && strcmp(type, "exec") && strcmp(type, "notify") &&
			strcmp(type, "notify-reload") && strcmp(type, "idle") && strcmp(type, "forking")) {
		fail(&t, fmt("Type=%s", type));
		goto bad;
	}
	out->kind = RC_SV_LONGRUN;
	if(prelude(&t, &b, "service", &out->needs, 0) != 0 || emit_cmds(&t, &b, "ExecStartPre", 0) != 0) {
		goto bad;
	}
	if(!strcmp(type, "forking")) {
		/* Dienst geht in den Hintergrund: über die PID-Datei beobachten */
		const char *pf = rc_unit_get(u, "Service", "PIDFile");
		char *pidfile;
		if(!pf) {
			fail(&t, fmt("Type=forking without PIDFile="));
			goto bad;
		}
		pidfile = specifiers(&t, pf);
		if(!pidfile) goto bad;
		if(emit_cmds(&t, &b, "ExecStart", 0) != 0) {
			free(pidfile);
			goto bad;
		}
		sb_puts(&b, "pf=");
		emit_quoted(&b, pidfile);
		sb_puts(&b, "\nn=0\nwhile [ ! -s \"$pf\" ] && [ $n -lt 30 ]; do sleep 1; n=$((n+1)); done\n"
				"pid=$(cat \"$pf\" 2>/dev/null) || exit 1\n"
				"while kill -0 \"$pid\" 2>/dev/null; do sleep 5; done\nexit 1\n");
		out->run = sb_take(&b);
		/* finish: beim Stoppen den Hintergrund-Prozess beenden */
		{
			sb_t f = { 0 };
			sb_printf(&f, "#!/bin/sh\n# rcyay-dienste: finish for %s (Type=forking)\n"
					"PATH=/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin\n", u->name);
			if(rc_unit_get(u, "Service", "ExecStop")) {
				sb_puts(&f, "[ \"$1\" = -1 ] || [ \"$1\" = 0 ] || [ \"$1\" = 1 ] || exit 0\n");
				if(emit_cmds(&t, &f, "ExecStop", 0) != 0) {
					free(f.s);
					free(pidfile);
					goto bad;
				}
			}
			sb_puts(&f, "pf=");
			emit_quoted(&f, pidfile);
			sb_puts(&f, "\n[ -s \"$pf\" ] && kill \"$(cat \"$pf\")\" 2>/dev/null\nexit 0\n");
			out->finish = sb_take(&f);
		}
		free(pidfile);
	} else {
		if(emit_cmds(&t, &b, "ExecStart", 1) != 0) goto bad;
		out->run = sb_take(&b);
	}
	if(rc_unit_get(u, "Service", "ExecStartPost")) {
		note(&t, fmt("%s: ExecStartPost= ignored for a supervised service", u->name));
	}
	return out->run ? 0 : -1;
bad:
	free(b.s);
	rc_svout_free(out);
	*why = fmt("%s: cannot translate: %s - add an own service in overlay/dienste/%s/",
			u->name, t.why ? t.why : "error", u->name);
	free(t.why);
	return -1;
}

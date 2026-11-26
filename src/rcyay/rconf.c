/*
 *  rconf.c - /etc/rcyay.conf
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
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

#include "config.h"
#include "ini.h"
#include "rcyay.h"


char *rc_expand_home(const char *path, const char *home)
{
	char *r = NULL;
	if(path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
		if(!home) {
			home = getenv("HOME");
		}
		if(!home) {
			struct passwd *pw = getpwuid(getuid());
			home = pw ? pw->pw_dir : "/";
		}
		if(asprintf(&r, "%s%s", home, path + 1) < 0) {
			r = NULL;
		}
		return r;
	}
	return strdup(path);
}

static int parse_bool(const char *v, int *out)
{
	if(!strcasecmp(v, "yes") || !strcasecmp(v, "ja") || !strcasecmp(v, "true") || !strcmp(v, "1")) {
		*out = 1;
	} else if(!strcasecmp(v, "no") || !strcasecmp(v, "nein") || !strcasecmp(v, "false") || !strcmp(v, "0")) {
		*out = 0;
	} else {
		return -1;
	}
	return 0;
}

static void set(char **dst, const char *v)
{
	free(*dst);
	*dst = strdup(v);
}

static int conf_cb(const char *file, int line, const char *section,
		char *key, char *value, void *data)
{
	int *removemake = data;
	if(!key && !value) {
		/* neuer Abschnitt */
		if(section && strcmp(section, "options") != 0 && strcmp(section, "rcyay") != 0) {
			rc_warn("%s:%d: unbekannter Abschnitt [%s]\n", file, line, section);
		}
		return 0;
	}
	if(!key) {
		return 0;
	}
	if(!value) {
		rc_warn("%s:%d: %s ohne Wert\n", file, line, key);
		return 0;
	}
	if(!strcmp(key, "AURURL")) set(&R.aururl, value);
	else if(!strcmp(key, "RepoGitURL")) set(&R.repogiturl, value);
	else if(!strcmp(key, "BuildDir")) set(&R.builddir, value);
	else if(!strcmp(key, "BuildUser")) set(&R.builduser, value);
	else if(!strcmp(key, "Sudo")) set(&R.sudo, value);
	else if(!strcmp(key, "Init")) set(&R.init, value);
	else if(!strcmp(key, "Variant")) set(&R.variant, value);
	else if(!strcmp(key, "RemoveMake")) {
		if(parse_bool(value, removemake) != 0) {
			rc_warn("%s:%d: RemoveMake: yes oder no erwartet\n", file, line);
		}
	} else {
		rc_warn("%s:%d: unbekannte Einstellung %s\n", file, line, key);
	}
	return 0;
}

int rc_readconf(void)
{
	const char *path = R.rcyayconf ? R.rcyayconf : getenv("RCYAY_CONF");
	int removemake = 1;
	int explicit = path != NULL;

	if(!path) {
		path = RCYAY_CONF;
	}
	if(access(path, R_OK) == 0) {
		if(parse_ini(path, conf_cb, &removemake) != 0) {
			return -1;
		}
	} else if(explicit) {
		rc_err("Konfiguration %s nicht lesbar: %s\n", path, strerror(errno));
		return -1;
	}
	if(R.removemake < 0) {
		R.removemake = removemake;
	}
	if(R.cli_aururl) set(&R.aururl, R.cli_aururl);
	if(R.cli_builddir) set(&R.builddir, R.cli_builddir);
	if(!R.aururl) R.aururl = strdup("https://aur.archlinux.org");
	if(!R.repogiturl) R.repogiturl = strdup("https://gitlab.archlinux.org/archlinux/packaging/packages");
	if(!R.builduser) R.builduser = strdup("rcyay-bau");
	if(!R.sudo) R.sudo = strdup(rc_have_program("sudo") || !rc_have_program("doas") ? "sudo" : "doas");
	if(!R.builddir) {
		R.builddir = strdup(geteuid() == 0 ? "/var/cache/rcyay" : "~/.cache/rcyay");
	}
	{
		char *b = rc_expand_home(R.builddir, NULL);
		free(R.builddir);
		R.builddir = b;
	}
	return R.builddir ? 0 : -1;
}

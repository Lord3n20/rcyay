/*
 *  rcyay.c - Einstieg: pacman-Operationen + AUR (wie yay)
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
#include <unistd.h>
#include <locale.h>
#include <limits.h>
#include <sys/utsname.h>

#include "config.h"
#include "conf.h"
#include "util.h"
#include "rcyay.h"

#if defined(ENABLE_NLS)
#include <libintl.h>
#endif

rcyay_t R;

static char *self_path(const char *argv0)
{
	char buf[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if(n > 0) {
		buf[n] = '\0';
		return strdup(buf);
	}
	if(strchr(argv0, '/')) {
		char *r = realpath(argv0, NULL);
		if(r) {
			return r;
		}
	}
	return strdup("rcyay");
}

static void useragent(void)
{
	char agent[256];
	struct utsname un;
	uname(&un);
	snprintf(agent, sizeof(agent), "rcyay/%s (%s %s) libalpm/%s", RCYAY_VERSION,
			un.sysname, un.machine, alpm_version());
	if(!R.ctx) {
		R.ctx = aur_ctx_new(R.aururl, agent);
	}
}

static int print_version(void)
{
	printf("rcyay v%s – pacman v%s, libalpm v%s\n", RCYAY_VERSION, PACKAGE_VERSION, alpm_version());
	printf(_("Copyright (C) 2026 rcyay developers; pacman (C) 2006-2024 Pacman Development Team\n"));
	printf(_("Free software under the GNU General Public License, version 2 or later.\n"));
	return 0;
}

int main(int argc, char *argv[])
{
	int ret;
	char *defargs[] = { argv[0], (char *)"-Syu", NULL };

	/* interner Aufruf: reines pacman (für sudo/Kindprozesse) */
	if(argc > 1 && strcmp(argv[1], "--rcyay-pacman") == 0) {
		argv[1] = argv[0];
		return pacman_main(argc - 1, argv + 1);
	}

#if defined(ENABLE_NLS)
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	bindtextdomain("rcyay", LOCALEDIR);
	textdomain(PACKAGE);
#endif

	/* stdin ungepuffert: Antworten, die für Kindprozesse (pacman -U/-S über sudo)
	 * gedacht sind, dürfen nicht in rcyays stdio-Puffer verschwinden */
	setvbuf(stdin, NULL, _IONBF, 0);

	R.myname = argv[0];
	R.self = self_path(argv[0]);
	R.removemake = -1;
	R.bottomup = -1;

	if(argc == 1) {
		argc = 2;
		argv = defargs;
	}
	if(rc_parseargs(argc, argv) != 0) {
		return 1;
	}

	/* rcyay-dienste (Init-neutral): --services, --enable/--disable, --init */
	if(R.svcmd) {
		if(rc_readconf() != 0) {
			return 1;
		}
		return rc_dienste_main();
	}

	/* VirtualProvides: --provides list|apply; vor -U das Metapaket anpassen */
	if(R.provcmd || (R.op == 'U' && !R.help && !R.cnt['p'])) {
		if(rc_readconf() != 0 || rc_alpm_init() != 0) {
			return 1;
		}
		if(R.provcmd) {
			ret = rc_vprov_main(R.provcmd);
			rc_alpm_release();
			return ret;
		}
		if(rc_vprov_sync() != 0) {
			rc_alpm_release();
			return 1;
		}
	}

	/* reine pacman-Operationen unverändert an pacman */
	if(R.op && strchr("DFQRTU", R.op)) {
		return rc_pacman_inproc(R.tokens, R.targets);
	}
	if(R.op == 'V') {
		return print_version();
	}
	if(R.help) {
		if(R.op == 'G' || R.op == 'Y' || R.op == 'P' || R.op == 0) {
			rc_usage();
		}
		if(R.op == 'S' || R.op == 0) {
			if(R.op == 'S') {
				rc_usage();
			}
			fflush(stdout);
			return rc_pacman_inproc(R.tokens, NULL);
		}
		return 0;
	}
	if(R.op == 0 && !R.targets) {
		return rc_pacman_inproc(R.tokens, NULL);
	}

	if(rc_readconf() != 0) {
		return 1;
	}
	useragent();
	if(!R.ctx) {
		return 1;
	}
	if(rc_alpm_init() != 0) {
		return 1;
	}

	switch(R.op) {
		case 'S':
			ret = rc_op_sync();
			break;
		case 'G':
			ret = rc_op_getpkgbuild();
			break;
		case 'Y':
			if(R.cnt['c']) {
				ret = rc_op_clean();
			} else {
				rc_err(_("-Y: only -Yc (remove unneeded dependencies) is supported\n"));
				ret = 1;
			}
			break;
		case 'P':
			if(R.cnt['s']) {
				ret = rc_op_stats();
			} else {
				rc_err(_("-P: only -Ps (statistics) is supported\n"));
				ret = 1;
			}
			break;
		case 0:
			ret = rc_op_interactive();
			break;
		default:
			ret = rc_pacman_inproc(R.tokens, R.targets);
	}
	rc_alpm_release();
	aur_ctx_free(R.ctx);
	return ret;
}

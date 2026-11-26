/*
 *  run.c - alpm-Zugriff, Kindprozesse, Nutzer, Dateisystem, Meldungen
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
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"
#include "conf.h"
#include "util.h"
#include "rcyay.h"

/* ---------- Meldungen ---------- */

static const char *col(int which)
{
	if(!config) {
		return "";
	}
	switch(which) {
		case 0: return config->colstr.colon;
		case 1: return config->colstr.warn;
		case 2: return config->colstr.err;
		case 3: return config->colstr.nocolor;
		default: return config->colstr.title;
	}
}

void rc_msg(const char *fmt, ...)
{
	va_list ap;
	fflush(stdout);
	fputs(config ? col(0) : ":: ", stdout);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fputs(col(3), stdout);
	fflush(stdout);
}

void rc_warn(const char *fmt, ...)
{
	va_list ap;
	fflush(stdout);
	fprintf(stderr, "%s%s%s", col(1), P_("warning: "), col(3));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void rc_err(const char *fmt, ...)
{
	va_list ap;
	fflush(stdout);
	fprintf(stderr, "%s%s%s", col(2), P_("error: "), col(3));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

int rc_confirm(int def_yes, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if(!config) {
		return def_yes;
	}
	config->noconfirm = R.noconfirm;
	return def_yes ? yesno("%s", buf) : noyes("%s", buf);
}

alpm_list_t *rc_list_dup(alpm_list_t *l)
{
	alpm_list_t *r = NULL;
	for(; l; l = l->next) {
		r = alpm_list_add(r, strdup(l->data));
	}
	return r;
}

alpm_list_t *rc_strlist(const char *first, ...)
{
	alpm_list_t *r = NULL;
	va_list ap;
	va_start(ap, first);
	for(const char *s = first; s; s = va_arg(ap, const char *)) {
		r = alpm_list_add(r, strdup(s));
	}
	va_end(ap);
	return r;
}

/* ---------- alpm ---------- */

int rc_alpm_init(void)
{
	alpm_list_t *a;
	if(config) {
		return 0;
	}
	if(!(config = config_new())) {
		return -1;
	}
	if(R.configfile) {
		free(config->configfile);
		config->configfile = strdup(R.configfile);
	}
	if(R.rootdir) {
		free(config->rootdir);
		config->rootdir = resolve_path(R.rootdir, "--root");
	}
	if(R.dbpath) {
		free(config->dbpath);
		config->dbpath = resolve_path(R.dbpath, "--dbpath");
	}
	if(R.sysroot) {
		free(config->sysroot);
		config->sysroot = resolve_path(R.sysroot, "--sysroot");
	}
	for(a = R.arches; a; a = a->next) {
		config_add_architecture(strdup(a->data));
	}
	if(R.color) {
		if(strcmp(R.color, "never") == 0) {
			config->color = PM_COLOR_OFF;
		} else if(strcmp(R.color, "always") == 0) {
			config->color = PM_COLOR_ON;
		} else {
			config->color = isatty(fileno(stdout)) ? PM_COLOR_ON : PM_COLOR_OFF;
		}
		enable_colors(config->color);
	}
	config->noconfirm = R.noconfirm;
	if(!isatty(fileno(stdout))) {
		config->noprogressbar = 1;
	}
	if(parseconfig(config->configfile) != 0) {
		config_free(config);
		config = NULL;
		return -1;
	}
	R.handle = config->handle;
	a = alpm_option_get_architectures(R.handle);
	R.arch = a ? a->data : "x86_64";
	return 0;
}

void rc_alpm_release(void)
{
	if(config) {
		if(config->handle) {
			alpm_release(config->handle);
			config->handle = NULL;
		}
		config_free(config);
		config = NULL;
		R.handle = NULL;
		R.arch = NULL;
	}
}

int rc_alpm_reload(void)
{
	rc_alpm_release();
	return rc_alpm_init();
}

/* ---------- Prozesse ---------- */

int rc_have_program(const char *name)
{
	const char *path = getenv("PATH");
	char *p, *save = NULL, *dir;
	int found = 0;
	if(strchr(name, '/')) {
		return access(name, X_OK) == 0;
	}
	if(!path) {
		path = "/usr/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
	}
	p = strdup(path);
	for(dir = strtok_r(p, ":", &save); dir && !found; dir = strtok_r(NULL, ":", &save)) {
		char *f;
		if(asprintf(&f, "%s/%s", *dir ? dir : ".", name) >= 0) {
			found = access(f, X_OK) == 0;
			free(f);
		}
	}
	free(p);
	return found;
}

/* Führt argv aus (PATH-Suche), optional in dir, als uid/gid (uid (uid_t)-1 =
 * unverändert), stdout optional in *out. Rückgabe: Exit-Code, 128+Signal
 * oder -1. */
static int run_quiet;

int rc_run_quiet(const char *const argv[], const char *dir, uid_t uid, gid_t gid, const char *home, char **out)
{
	int r;
	run_quiet = 1;
	r = rc_run(argv, dir, uid, gid, home, out);
	run_quiet = 0;
	return r;
}

int rc_run(const char *const argv[], const char *dir, uid_t uid, gid_t gid, const char *home, char **out)
{
	int pfd[2] = { -1, -1 };
	pid_t pid;
	int status;
	struct sigaction sa, oldint, oldquit;

	fflush(stdout);
	fflush(stderr);
	if(out) {
		*out = NULL;
		if(pipe(pfd) != 0) {
			rc_err("pipe: %s\n", strerror(errno));
			return -1;
		}
	}
	/* Strg+C soll das Kind beenden, rcyay wertet danach aus */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGINT, &sa, &oldint);
	sigaction(SIGQUIT, &sa, &oldquit);
	pid = fork();
	if(pid < 0) {
		rc_err("fork: %s\n", strerror(errno));
		sigaction(SIGINT, &oldint, NULL);
		sigaction(SIGQUIT, &oldquit, NULL);
		return -1;
	}
	if(pid == 0) {
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		if(run_quiet) {
			int nul = open("/dev/null", O_WRONLY);
			if(nul >= 0) {
				dup2(nul, STDERR_FILENO);
				close(nul);
			}
		}
		if(out) {
			close(pfd[0]);
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
		}
		if(uid != (uid_t)-1 && uid != geteuid()) {
			struct passwd *pw = getpwuid(uid);
			if((pw && initgroups(pw->pw_name, gid) != 0) || (!pw && setgroups(0, NULL) != 0) ||
					setgid(gid) != 0 || setuid(uid) != 0) {
				fprintf(stderr, _("rcyay: switching user failed: %s\n"), strerror(errno));
				_exit(127);
			}
			if(pw) {
				setenv("USER", pw->pw_name, 1);
				setenv("LOGNAME", pw->pw_name, 1);
			}
			if(home) {
				setenv("HOME", home, 1);
			}
			unsetenv("SUDO_USER");
			unsetenv("SUDO_UID");
			unsetenv("SUDO_GID");
			unsetenv("SUDO_COMMAND");
			unsetenv("XDG_RUNTIME_DIR");
		}
		if(dir && chdir(dir) != 0) {
			fprintf(stderr, "rcyay: %s: %s\n", dir, strerror(errno));
			_exit(127);
		}
		execvp(argv[0], (char *const *)argv);
		fprintf(stderr, "rcyay: %s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	if(out) {
		char *buf = NULL;
		size_t len = 0, cap = 0;
		ssize_t n;
		close(pfd[1]);
		for(;;) {
			if(cap - len < 4096) {
				char *nb = realloc(buf, cap ? cap * 2 : 8192);
				if(!nb) break;
				buf = nb;
				cap = cap ? cap * 2 : 8192;
			}
			n = read(pfd[0], buf + len, cap - len - 1);
			if(n < 0 && errno == EINTR) continue;
			if(n <= 0) break;
			len += (size_t)n;
		}
		close(pfd[0]);
		if(buf) {
			buf[len] = '\0';
		}
		*out = buf ? buf : strdup("");
	}
	while(waitpid(pid, &status, 0) < 0) {
		if(errno != EINTR) {
			status = -1;
			break;
		}
	}
	sigaction(SIGINT, &oldint, NULL);
	sigaction(SIGQUIT, &oldquit, NULL);
	if(status == -1) {
		return -1;
	}
	if(WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	if(WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	}
	return -1;
}

/* pacman (eingebaut) als Kindprozess; root = braucht Root-Rechte */
int rc_pacman(alpm_list_t *args, int root)
{
	size_t n = alpm_list_count(args) + 4;
	const char **v = calloc(n, sizeof(char *));
	int k = 0, ret;
	if(!v) {
		return -1;
	}
	if(root && geteuid() != 0) {
		if(!rc_have_program(R.sudo)) {
			rc_err(_("this step needs root privileges, but %s is missing\n"), R.sudo);
			free(v);
			return 1;
		}
		v[k++] = R.sudo;
	}
	v[k++] = R.self;
	v[k++] = "--rcyay-pacman";
	for(alpm_list_t *i = args; i; i = i->next) {
		v[k++] = i->data;
	}
	v[k] = NULL;
	ret = rc_run(v, NULL, (uid_t)-1, (gid_t)-1, NULL, NULL);
	free(v);
	return ret;
}

int rc_pacman_inproc(alpm_list_t *tokens, alpm_list_t *targets)
{
	int argc;
	char **argv;
	rc_alpm_release();
	argv = rc_pacman_argv(NULL, tokens, targets, &argc);
	if(!argv) {
		return 1;
	}
	return pacman_main(argc, argv);
}

/* ---------- Bau-Nutzer ---------- */

int rc_build_user(uid_t *uid, gid_t *gid, char **home)
{
	struct passwd *pw;
	if(geteuid() != 0) {
		pw = getpwuid(geteuid());
		*uid = geteuid();
		*gid = getegid();
		*home = strdup(pw && pw->pw_dir ? pw->pw_dir : (getenv("HOME") ? getenv("HOME") : "/tmp"));
		return 0;
	}
	pw = getpwnam(R.builduser);
	if(!pw) {
		char *homedir = NULL;
		int ret = -1;
		if(asprintf(&homedir, "/var/lib/%s", R.builduser) < 0) {
			return -1;
		}
		rc_msg(_("Creating build user %s (makepkg never runs as root)\n"), R.builduser);
		if(rc_have_program("useradd")) {
			const char *argv[] = { "useradd", "-r", "-m", "-d", homedir, "-s", "/bin/sh",
				"-c", "rcyay build user", R.builduser, NULL };
			ret = rc_run(argv, NULL, (uid_t)-1, (gid_t)-1, NULL, NULL);
		} else if(rc_have_program("adduser")) {
			const char *argv[] = { "adduser", "-S", "-D", "-h", homedir, "-s", "/bin/sh",
				R.builduser, NULL };
			ret = rc_run(argv, NULL, (uid_t)-1, (gid_t)-1, NULL, NULL);
		} else {
			rc_err(_("neither useradd nor adduser found - build user %s is missing\n"), R.builduser);
		}
		free(homedir);
		if(ret != 0) {
			rc_err(_("could not create build user %s\n"), R.builduser);
			return -1;
		}
		pw = getpwnam(R.builduser);
		if(!pw) {
			rc_err(_("build user %s is missing after creating it\n"), R.builduser);
			return -1;
		}
	}
	if(pw->pw_uid == 0) {
		rc_err(_("build user %s has UID 0 - makepkg must not run as root\n"), R.builduser);
		return -1;
	}
	*uid = pw->pw_uid;
	*gid = pw->pw_gid;
	*home = strdup(pw->pw_dir && *pw->pw_dir ? pw->pw_dir : "/var/lib/rcyay-bau");
	if(access(*home, F_OK) != 0) {
		rc_mkdir_p(*home, 0700);
		if(chown(*home, *uid, *gid) != 0) {
			rc_warn("%s: %s\n", *home, strerror(errno));
		}
	}
	return 0;
}

/* ---------- Dateisystem ---------- */

int rc_mkdir_p(const char *dir, mode_t mode)
{
	char *d = strdup(dir), *p;
	if(!d) {
		return -1;
	}
	for(p = d + 1; *p; p++) {
		if(*p == '/') {
			*p = '\0';
			if(mkdir(d, 0755) != 0 && errno != EEXIST) {
				free(d);
				return -1;
			}
			*p = '/';
		}
	}
	if(mkdir(d, mode) != 0 && errno != EEXIST) {
		free(d);
		return -1;
	}
	free(d);
	return 0;
}

static uid_t chown_uid;
static gid_t chown_gid;

static int chown_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st; (void)flag; (void)ftw;
	if(lchown(path, chown_uid, chown_gid) != 0) {
		rc_warn("chown %s: %s\n", path, strerror(errno));
	}
	return 0;
}

int rc_chown_r(const char *path, uid_t uid, gid_t gid)
{
	chown_uid = uid;
	chown_gid = gid;
	return nftw(path, chown_cb, 32, FTW_PHYS);
}

static int rm_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st; (void)ftw;
	if(flag == FTW_DP) {
		return rmdir(path) != 0 ? -1 : 0;
	}
	return unlink(path) != 0 ? -1 : 0;
}

int rc_rm_rf(const char *path)
{
	struct stat st;
	if(lstat(path, &st) != 0) {
		return errno == ENOENT ? 0 : -1;
	}
	return nftw(path, rm_cb, 32, FTW_DEPTH | FTW_PHYS);
}

/* PATH-Einträge aus add an *path anhängen, die dort noch fehlen */
static void path_merge(char **path, const char *add)
{
	const char *a = add;
	while(a && *a) {
		const char *e = strchr(a, ':');
		size_t n = e ? (size_t)(e - a) : strlen(a);
		if(n > 0 && n < PATH_MAX) {
			char comp[PATH_MAX];
			const char *p = *path;
			int found = 0;
			memcpy(comp, a, n);
			comp[n] = '\0';
			while(p && *p) {
				const char *pe = strchr(p, ':');
				size_t pn = pe ? (size_t)(pe - p) : strlen(p);
				if(pn == n && strncmp(p, comp, n) == 0) {
					found = 1;
					break;
				}
				p = pe ? pe + 1 : NULL;
			}
			if(!found) {
				char *np = NULL;
				if(asprintf(&np, "%s%s%s", *path && **path ? *path : "",
							*path && **path ? ":" : "", comp) >= 0) {
					free(*path);
					*path = np;
				}
			}
		}
		a = e ? e + 1 : NULL;
	}
}

/* Bauumgebung: PATH wie in einer Login-Shell des Bau-Nutzers ergänzen.
 * sudo (secure_path) und runuser liefern nur einen knappen PATH; Arch setzt
 * z. B. /usr/bin/core_perl (pod2man) erst über /etc/profile.d/perlbin.sh.
 * Vorhandene Einträge des Aufrufers bleiben vorn. */
void rc_build_path(uid_t uid, gid_t gid, const char *home)
{
	static int done;
	char *path, *login = NULL;
	const char *argv[] = { "env", "-i", NULL, "/bin/sh", "-lc", "printf %s \"$PATH\"", NULL };
	char *homeenv = NULL;

	if(done) {
		return;
	}
	done = 1;
	path = strdup(getenv("PATH") ? getenv("PATH") : "");
	if(!path) {
		return;
	}
	if(asprintf(&homeenv, "HOME=%s", home ? home : "/") >= 0) {
		argv[2] = homeenv;
		if(access("/bin/sh", X_OK) == 0 &&
				rc_run_quiet(argv, "/", uid, gid, home, &login) == 0 && login) {
			path_merge(&path, login);
		}
	}
	free(homeenv);
	free(login);
	/* Rückfall ohne /etc/profile: Arch-Perl-Pfade, sofern vorhanden */
	{
		const char *perl[] = { "/usr/bin/site_perl", "/usr/bin/vendor_perl", "/usr/bin/core_perl", NULL };
		for(int i = 0; perl[i]; i++) {
			if(access(perl[i], X_OK) == 0) {
				path_merge(&path, perl[i]);
			}
		}
	}
	setenv("PATH", path, 1);
	free(path);
}

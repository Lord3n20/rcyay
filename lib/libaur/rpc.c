/*
 *  rpc.c - AUR-RPC v5, Downloads und Snapshots für libaur
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
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>

#include "rpc.h"
#include "json.h"
#include "aurutil.h"
#include "aurintl.h"

/* Maximale URL-Länge einer info-Anfrage (aurweb lehnt sehr lange URIs ab) */
#define AUR_MAX_URI 4000

static void ctx_err(aur_ctx_t *ctx, const char *fmt, ...)
{
	va_list ap;
	if(!ctx) {
		return;
	}
	va_start(ap, fmt);
	vsnprintf(ctx->error, sizeof(ctx->error), fmt, ap);
	va_end(ap);
}

aur_ctx_t *aur_ctx_new(const char *baseurl, const char *useragent)
{
	aur_ctx_t *ctx = calloc(1, sizeof(*ctx));
	size_t n;
	if(!ctx) {
		return NULL;
	}
	ctx->baseurl = strdup(baseurl ? baseurl : "https://aur.archlinux.org");
	n = strlen(ctx->baseurl);
	while(n > 0 && ctx->baseurl[n - 1] == '/') {
		ctx->baseurl[--n] = '\0';
	}
	ctx->useragent = strdup(useragent ? useragent : "libaur");
	ctx->timeout = 60;
	return ctx;
}

void aur_ctx_free(aur_ctx_t *ctx)
{
	if(ctx) {
		free(ctx->baseurl);
		free(ctx->useragent);
		free(ctx);
	}
}

void aur_pkg_free(aur_pkg_t *p)
{
	if(!p) {
		return;
	}
	free(p->name);
	free(p->pkgbase);
	free(p->version);
	free(p->desc);
	free(p->url);
	free(p->urlpath);
	free(p->maintainer);
	free(p->submitter);
	FREELIST(p->depends);
	FREELIST(p->makedepends);
	FREELIST(p->checkdepends);
	FREELIST(p->optdepends);
	FREELIST(p->provides);
	FREELIST(p->conflicts);
	FREELIST(p->replaces);
	FREELIST(p->groups);
	FREELIST(p->licenses);
	FREELIST(p->keywords);
	FREELIST(p->comaintainers);
	free(p);
}

void aur_pkglist_free(alpm_list_t *pkgs)
{
	alpm_list_free_inner(pkgs, (alpm_list_fn_free)aur_pkg_free);
	alpm_list_free(pkgs);
}

aur_pkg_t *aur_pkglist_find(alpm_list_t *pkgs, const char *name)
{
	for(alpm_list_t *i = pkgs; i; i = i->next) {
		aur_pkg_t *p = i->data;
		if(strcmp(p->name, name) == 0) {
			return p;
		}
	}
	return NULL;
}

static char *dupstr(const aur_json_t *obj, const char *key)
{
	const char *s = aur_json_get_str(obj, key);
	return s ? strdup(s) : NULL;
}

static alpm_list_t *strlist(const aur_json_t *obj, const char *key)
{
	alpm_list_t *l = NULL;
	aur_json_t *a = aur_json_get(obj, key);
	if(!a || a->type != AUR_JSON_ARRAY) {
		return NULL;
	}
	for(aur_json_t *c = a->child; c; c = c->next) {
		if(c->type == AUR_JSON_STRING) {
			l = alpm_list_add(l, strdup(c->string));
		}
	}
	return l;
}

static int set_err(char **err, const char *prefix, const char *arg)
{
	if(err && asprintf(err, "%s%s", prefix, arg ? arg : "") < 0) {
		*err = NULL;
	}
	return -1;
}

int aur_rpc_parse(const char *json, size_t len, alpm_list_t **out, char **err)
{
	const char *perr = NULL, *type;
	aur_json_t *root, *results;
	alpm_list_t *pkgs = NULL;

	*out = NULL;
	if(err) {
		*err = NULL;
	}
	root = aur_json_parse(json, len, &perr);
	if(!root) {
		return set_err(err, _("invalid JSON from AUR: "), perr);
	}
	type = aur_json_get_str(root, "type");
	if(!type) {
		aur_json_free(root);
		return set_err(err, _("invalid AUR response: "), _("no type"));
	}
	if(strcmp(type, "error") == 0) {
		set_err(err, _("AUR: "), aur_json_get_str(root, "error"));
		aur_json_free(root);
		return -1;
	}
	results = aur_json_get(root, "results");
	if(results && results->type == AUR_JSON_ARRAY) {
		for(aur_json_t *r = results->child; r; r = r->next) {
			aur_pkg_t *p;
			const char *name = aur_json_get_str(r, "Name");
			aur_json_t *ood;
			if(r->type != AUR_JSON_OBJECT || !name) {
				continue;
			}
			p = calloc(1, sizeof(*p));
			if(!p) {
				break;
			}
			p->name = strdup(name);
			p->pkgbase = dupstr(r, "PackageBase");
			if(!p->pkgbase) {
				p->pkgbase = strdup(name);
			}
			p->version = dupstr(r, "Version");
			if(!p->version) {
				p->version = strdup("0-0");
			}
			p->desc = dupstr(r, "Description");
			p->url = dupstr(r, "URL");
			p->urlpath = dupstr(r, "URLPath");
			p->maintainer = dupstr(r, "Maintainer");
			p->submitter = dupstr(r, "Submitter");
			p->votes = (long)aur_json_get_num(r, "NumVotes", 0);
			p->popularity = aur_json_get_num(r, "Popularity", 0);
			ood = aur_json_get(r, "OutOfDate");
			p->outofdate = (long)aur_json_num(ood, 0);
			p->firstsubmitted = (long)aur_json_get_num(r, "FirstSubmitted", 0);
			p->lastmodified = (long)aur_json_get_num(r, "LastModified", 0);
			p->depends = strlist(r, "Depends");
			p->makedepends = strlist(r, "MakeDepends");
			p->checkdepends = strlist(r, "CheckDepends");
			p->optdepends = strlist(r, "OptDepends");
			p->provides = strlist(r, "Provides");
			p->conflicts = strlist(r, "Conflicts");
			p->replaces = strlist(r, "Replaces");
			p->groups = strlist(r, "Groups");
			p->licenses = strlist(r, "License");
			p->keywords = strlist(r, "Keywords");
			p->comaintainers = strlist(r, "CoMaintainers");
			pkgs = alpm_list_add(pkgs, p);
		}
	}
	aur_json_free(root);
	*out = pkgs;
	return 0;
}

struct membuf {
	char *data;
	size_t len;
	size_t cap;
};

static size_t write_mem(char *ptr, size_t size, size_t nmemb, void *ud)
{
	struct membuf *mb = ud;
	size_t n = size * nmemb;
	if(mb->len + n + 1 > mb->cap) {
		size_t cap = mb->cap ? mb->cap : 16384;
		char *nd;
		while(cap < mb->len + n + 1) {
			cap *= 2;
		}
		nd = realloc(mb->data, cap);
		if(!nd) {
			return 0;
		}
		mb->data = nd;
		mb->cap = cap;
	}
	memcpy(mb->data + mb->len, ptr, n);
	mb->len += n;
	mb->data[mb->len] = '\0';
	return n;
}

static int curl_ready(void)
{
	static int done = 0;
	if(!done) {
		if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
			return -1;
		}
		done = 1;
	}
	return 0;
}

static int do_get(aur_ctx_t *ctx, const char *url, curl_write_callback cb, void *ud, long *status)
{
	CURL *c;
	CURLcode rc;
	long code = 0;
	char errbuf[CURL_ERROR_SIZE] = "";

	if(status) {
		*status = 0;
	}
	if(curl_ready() != 0 || !(c = curl_easy_init())) {
		ctx_err(ctx, _("curl initialisation failed"));
		return -1;
	}
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, ctx->useragent);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, ctx->timeout > 0 ? ctx->timeout * 5 : 0L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, ctx->timeout);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, ud);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	if(rc != CURLE_OK) {
		ctx_err(ctx, "%s: %s", url, errbuf[0] ? errbuf : curl_easy_strerror(rc));
		return -1;
	}
	if(status) {
		*status = code;
	}
	if(code != 200) {
		ctx_err(ctx, _("%s: HTTP %ld"), url, code);
		return -1;
	}
	return 0;
}

int aur_http_get(aur_ctx_t *ctx, const char *url, char **body, size_t *len, long *status)
{
	struct membuf mb = { NULL, 0, 0 };
	*body = NULL;
	if(do_get(ctx, url, (curl_write_callback)write_mem, &mb, status) != 0) {
		free(mb.data);
		return -1;
	}
	if(!mb.data) {
		mb.data = strdup("");
	}
	*body = mb.data;
	if(len) {
		*len = mb.len;
	}
	return 0;
}

static size_t write_file(char *ptr, size_t size, size_t nmemb, void *ud)
{
	return fwrite(ptr, size, nmemb, ud) * size;
}

int aur_http_download(aur_ctx_t *ctx, const char *url, const char *path, long *status)
{
	char *part;
	FILE *fp;
	int ret;

	if(asprintf(&part, "%s.part", path) < 0) {
		return -1;
	}
	fp = fopen(part, "w");
	if(!fp) {
		ctx_err(ctx, "%s: %s", part, strerror(errno));
		free(part);
		return -1;
	}
	ret = do_get(ctx, url, (curl_write_callback)write_file, fp, status);
	if(fclose(fp) != 0 && ret == 0) {
		ctx_err(ctx, "%s: %s", part, strerror(errno));
		ret = -1;
	}
	if(ret == 0 && rename(part, path) != 0) {
		ctx_err(ctx, "%s: %s", path, strerror(errno));
		ret = -1;
	}
	if(ret != 0) {
		unlink(part);
	}
	free(part);
	return ret;
}

static char *escape(const char *s)
{
	char *e, *r;
	if(curl_ready() != 0) {
		return NULL;
	}
	e = curl_easy_escape(NULL, s, 0);
	if(!e) {
		return NULL;
	}
	r = strdup(e);
	curl_free(e);
	return r;
}

static int rpc_request(aur_ctx_t *ctx, const char *url, alpm_list_t **out)
{
	char *body = NULL, *err = NULL;
	size_t len = 0;
	long status = 0;
	int ret;

	if(aur_http_get(ctx, url, &body, &len, &status) != 0) {
		/* aurweb liefert Fehler als JSON auch mit HTTP-Fehlercode – die
		 * Meldung aus ctx->error genügt hier. */
		return -1;
	}
	ret = aur_rpc_parse(body, len, out, &err);
	if(ret != 0) {
		ctx_err(ctx, "%s", err ? err : _("invalid AUR response"));
	}
	free(err);
	free(body);
	return ret;
}

int aur_info(aur_ctx_t *ctx, alpm_list_t *names, alpm_list_t **out)
{
	alpm_list_t *all = NULL, *i = names;

	*out = NULL;
	while(i) {
		char *url = NULL;
		size_t ulen;
		alpm_list_t *part = NULL;
		int n = 0;

		if(asprintf(&url, "%s/rpc/v5/info", ctx->baseurl) < 0) {
			goto oom;
		}
		ulen = strlen(url);
		for(; i; i = i->next) {
			char *e = escape(i->data), *nu;
			if(!e) {
				free(url);
				goto oom;
			}
			if(n > 0 && ulen + strlen(e) + 8 > AUR_MAX_URI) {
				free(e);
				break;
			}
			if(asprintf(&nu, "%s%sarg[]=%s", url, n ? "&" : "?", e) < 0) {
				free(e);
				free(url);
				goto oom;
			}
			free(e);
			free(url);
			url = nu;
			ulen = strlen(url);
			n++;
		}
		if(rpc_request(ctx, url, &part) != 0) {
			free(url);
			aur_pkglist_free(all);
			return -1;
		}
		free(url);
		all = alpm_list_join(all, part);
	}
	*out = all;
	return 0;
oom:
	ctx_err(ctx, _("out of memory"));
	aur_pkglist_free(all);
	return -1;
}

int aur_search(aur_ctx_t *ctx, const char *term, const char *by, alpm_list_t **out)
{
	char *e = escape(term), *eb = escape(by ? by : "name-desc"), *url = NULL;
	int ret;
	*out = NULL;
	if(!e || !eb || asprintf(&url, "%s/rpc/v5/search/%s?by=%s", ctx->baseurl, e, eb) < 0) {
		free(e);
		free(eb);
		ctx_err(ctx, _("out of memory"));
		return -1;
	}
	free(e);
	free(eb);
	ret = rpc_request(ctx, url, out);
	free(url);
	return ret;
}

char *aur_snapshot_url(aur_ctx_t *ctx, const aur_pkg_t *pkg)
{
	char *url = NULL;
	if(pkg->urlpath && pkg->urlpath[0] == '/') {
		if(asprintf(&url, "%s%s", ctx->baseurl, pkg->urlpath) < 0) {
			url = NULL;
		}
	} else if(pkg->urlpath && strstr(pkg->urlpath, "://")) {
		url = strdup(pkg->urlpath);
	} else {
		char *e = escape(pkg->pkgbase);
		if(e && asprintf(&url, "%s/cgit/aur.git/snapshot/%s.tar.gz", ctx->baseurl, e) < 0) {
			url = NULL;
		}
		free(e);
	}
	return url;
}

char *aur_plain_url(aur_ctx_t *ctx, const char *pkgbase, const char *file)
{
	char *url = NULL, *e = escape(pkgbase);
	if(e && asprintf(&url, "%s/cgit/aur.git/plain/%s?h=%s", ctx->baseurl, file, e) < 0) {
		url = NULL;
	}
	free(e);
	return url;
}

/* Entfernt strip führende Pfadteile; NULL, wenn nichts übrig bleibt oder der
 * Pfad unsicher ist. Ergebnis zeigt in path. */
static const char *strip_path(const char *path, int strip, int *unsafe)
{
	const char *p = path;
	*unsafe = 0;
	if(*p == '/') {
		*unsafe = 1;
		return NULL;
	}
	while(strip > 0) {
		const char *s = strchr(p, '/');
		if(!s) {
			return NULL;
		}
		p = s + 1;
		while(*p == '/') p++;
		strip--;
	}
	if(*p == '\0') {
		return NULL;
	}
	/* ".." als Pfadteil ablehnen */
	for(const char *q = p; *q; ) {
		size_t n = strcspn(q, "/");
		if(n == 2 && q[0] == '.' && q[1] == '.') {
			*unsafe = 1;
			return NULL;
		}
		q += n;
		while(*q == '/') q++;
	}
	return p;
}

static int mkdir_p(const char *dir)
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
	if(mkdir(d, 0755) != 0 && errno != EEXIST) {
		free(d);
		return -1;
	}
	free(d);
	return 0;
}

int aur_extract(const char *archive, const char *dest, int strip, char *err, size_t errlen)
{
	struct archive *a, *ext;
	struct archive_entry *entry;
	int r = ARCHIVE_OK, ret = 0, flags, cwd;
	unsigned long n = 0;
	char *absarchive;

	if(mkdir_p(dest) != 0) {
		snprintf(err, errlen, "%s: %s", dest, strerror(errno));
		return -1;
	}
	/* relativ zu dest entpacken, damit libarchive alle Sicherheitsprüfungen
	 * (absolute Pfade, "..", Symlinks) auf die Archivpfade anwenden kann */
	absarchive = archive[0] == '/' ? strdup(archive) : realpath(archive, NULL);
	if(!absarchive) {
		snprintf(err, errlen, "%s: %s", archive, strerror(errno));
		return -1;
	}
	/* O_PATH: klappt auch, wenn das aktuelle Verzeichnis nicht lesbar ist
	 * (z. B. runuser/sudo -u aus /root heraus) */
	cwd = open(".", O_PATH | O_DIRECTORY | O_CLOEXEC);
	if(chdir(dest) != 0) {
		snprintf(err, errlen, "%s: %s", dest, strerror(errno));
		if(cwd >= 0) close(cwd);
		free(absarchive);
		return -1;
	}
	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	ext = archive_write_disk_new();
	flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_SYMLINKS |
		ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);

	if(archive_read_open_filename(a, absarchive, 10240) != ARCHIVE_OK) {
		snprintf(err, errlen, "%s: %s", archive, archive_error_string(a));
		ret = -1;
		goto done;
	}
	while((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
		const char *rel, *hl;
		char *full = NULL, *hfull = NULL;
		int unsafe;
		mode_t type = archive_entry_filetype(entry);

		rel = strip_path(archive_entry_pathname(entry), strip, &unsafe);
		if(unsafe) {
			snprintf(err, errlen, _("%s: unsafe path %s"), archive, archive_entry_pathname(entry));
			ret = -1;
			break;
		}
		if(!rel) {
			continue; /* oberster Ordner selbst */
		}
		if(type != AE_IFREG && type != AE_IFDIR && type != AE_IFLNK) {
			continue; /* Geräte, FIFOs … gehören nicht in einen Bauordner */
		}
		if(!(full = strdup(rel))) {
			ret = -1;
			break;
		}
		archive_entry_set_pathname(entry, full);
		hl = archive_entry_hardlink(entry);
		if(hl) {
			const char *hrel = strip_path(hl, strip, &unsafe);
			if(!hrel || !(hfull = strdup(hrel))) {
				free(full);
				snprintf(err, errlen, _("%s: unsafe hardlink %s"), archive, hl);
				ret = -1;
				break;
			}
			archive_entry_set_hardlink(entry, hfull);
		}
		/* Besitzer nie übernehmen; Rechte ohne setuid/setgid */
		archive_entry_set_perm(entry, archive_entry_perm(entry) & 0777);
		r = archive_write_header(ext, entry);
		if(r == ARCHIVE_OK && archive_entry_size(entry) > 0) {
			const void *buf;
			size_t size;
			la_int64_t off;
			while((r = archive_read_data_block(a, &buf, &size, &off)) == ARCHIVE_OK) {
				if(archive_write_data_block(ext, buf, size, off) != ARCHIVE_OK) {
					r = ARCHIVE_FATAL;
					break;
				}
			}
			if(r == ARCHIVE_EOF) {
				r = ARCHIVE_OK;
			}
		}
		if(r == ARCHIVE_OK) {
			r = archive_write_finish_entry(ext);
		}
		free(full);
		free(hfull);
		n++;
		if(r != ARCHIVE_OK && r != ARCHIVE_WARN) {
			snprintf(err, errlen, "%s: %s", archive,
					archive_error_string(ext) ? archive_error_string(ext) : archive_error_string(a));
			ret = -1;
			break;
		}
	}
	if(ret == 0 && r != ARCHIVE_EOF) {
		snprintf(err, errlen, "%s: %s", archive, archive_error_string(a));
		ret = -1;
	}
	/* libarchive liest eine leere Datei (HTTP 200 ohne Körper) als gültiges
	 * leeres Archiv – ohne einen einzigen Eintrag ist das kein Snapshot */
	if(ret == 0 && n == 0) {
		snprintf(err, errlen, _("%s: archive is empty"), archive);
		ret = -1;
	}
done:
	archive_read_free(a);
	archive_write_free(ext);
	if(cwd >= 0) {
		if(fchdir(cwd) != 0 && ret == 0) {
			snprintf(err, errlen, "fchdir: %s", strerror(errno));
			ret = -1;
		}
		close(cwd);
	} else if(chdir("/") != 0) {
		ret = -1;
	}
	free(absarchive);
	return ret;
}

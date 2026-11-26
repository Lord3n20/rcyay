#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include "check.h"
#include "rpc.h"
#include "aurutil.h"

static const char *INFO =
	"{\"resultcount\":2,\"results\":["
	"{\"Name\":\"yay-bin\",\"PackageBase\":\"yay-bin\",\"Version\":\"12.4.2-1\","
	"\"Description\":\"Yet another yogurt\",\"URLPath\":\"/cgit/aur.git/snapshot/yay-bin.tar.gz\","
	"\"NumVotes\":512,\"Popularity\":12.5,\"OutOfDate\":null,\"Maintainer\":\"jguer\","
	"\"Depends\":[\"pacman>6.1\",\"git\"],\"Provides\":[\"yay\"],\"Conflicts\":[\"yay\"],"
	"\"License\":[\"GPL-3.0-or-later\"]},"
	"{\"Name\":\"split-b\",\"PackageBase\":\"split\",\"Version\":\"2:1.0-3\",\"OutOfDate\":1700000000}"
	"],\"type\":\"multiinfo\",\"version\":5}";

static void make_tar(const char *path, const char **names)
{
	struct archive *a = archive_write_new();
	archive_write_add_filter_gzip(a);
	archive_write_set_format_pax_restricted(a);
	archive_write_open_filename(a, path);
	for(; *names; names++) {
		struct archive_entry *e = archive_entry_new();
		size_t len = strlen(*names);
		archive_entry_set_pathname(e, *names);
		if((*names)[len - 1] == '/') {
			archive_entry_set_filetype(e, AE_IFDIR);
			archive_entry_set_perm(e, 0755);
			archive_write_header(a, e);
		} else {
			archive_entry_set_filetype(e, AE_IFREG);
			archive_entry_set_perm(e, 04755);
			archive_entry_set_size(e, 5);
			archive_write_header(a, e);
			archive_write_data(a, "hallo", 5);
		}
		archive_entry_free(e);
	}
	archive_write_close(a);
	archive_write_free(a);
}

int main(void)
{
	alpm_list_t *pkgs = NULL;
	char *err = NULL;
	aur_pkg_t *p;
	aur_ctx_t *ctx;
	char *url;

	CHECK(aur_rpc_parse(INFO, strlen(INFO), &pkgs, &err) == 0);
	CHECK(alpm_list_count(pkgs) == 2);
	p = aur_pkglist_find(pkgs, "yay-bin");
	CHECK(p != NULL);
	if(p) {
		CHECK_STR(p->version, "12.4.2-1");
		CHECK(p->votes == 512);
		CHECK(p->popularity > 12.4 && p->popularity < 12.6);
		CHECK(p->outofdate == 0);
		CHECK(alpm_list_count(p->depends) == 2);
		CHECK(alpm_list_find_str(p->provides, "yay") != NULL);
		CHECK_STR(p->licenses->data, "GPL-3.0-or-later");
	}
	p = aur_pkglist_find(pkgs, "split-b");
	CHECK(p && strcmp(p->pkgbase, "split") == 0 && p->outofdate == 1700000000);

	ctx = aur_ctx_new("http://127.0.0.1:8099///", "test");
	CHECK_STR(ctx->baseurl, "http://127.0.0.1:8099");
	url = aur_snapshot_url(ctx, aur_pkglist_find(pkgs, "yay-bin"));
	CHECK_STR(url, "http://127.0.0.1:8099/cgit/aur.git/snapshot/yay-bin.tar.gz");
	free(url);
	url = aur_snapshot_url(ctx, aur_pkglist_find(pkgs, "split-b"));
	CHECK_STR(url, "http://127.0.0.1:8099/cgit/aur.git/snapshot/split.tar.gz");
	free(url);
	url = aur_plain_url(ctx, "lib32-gcc+x", "PKGBUILD");
	CHECK_STR(url, "http://127.0.0.1:8099/cgit/aur.git/plain/PKGBUILD?h=lib32-gcc%2Bx");
	free(url);
	aur_ctx_free(ctx);
	aur_pkglist_free(pkgs);

	pkgs = NULL;
	{
		const char *e1 = "{\"type\":\"error\",\"error\":\"Too many package results.\",\"resultcount\":0,\"results\":[]}";
		CHECK(aur_rpc_parse(e1, strlen(e1), &pkgs, &err) == -1);
	}
	CHECK(err && strstr(err, "Too many"));
	free(err);
	CHECK(aur_rpc_parse("<html>", 6, &pkgs, &err) == -1);
	free(err);
	{
		const char *e2 = "{\"type\":\"search\",\"resultcount\":0,\"results\":[]}";
		CHECK(aur_rpc_parse(e2, strlen(e2), &pkgs, &err) == 0 && pkgs == NULL);
	}

	/* Abhängigkeiten / Versionen */
	{
		alpm_list_t *prov = NULL;
		prov = alpm_list_add(prov, "libfoo.so=2-64");
		prov = alpm_list_add(prov, "bar=1.5");
		prov = alpm_list_add(prov, "baz");
		CHECK(aur_dep_satisfied("foo", "foo", "1-1", NULL));
		CHECK(aur_dep_satisfied("foo>=1.0", "foo", "1.2-1", NULL));
		CHECK(!aur_dep_satisfied("foo>2", "foo", "1.2-1", NULL));
		CHECK(aur_dep_satisfied("foo<2", "foo", "1.2-1", NULL));
		CHECK(aur_dep_satisfied("foo=1.2", "foo", "1.2-1", NULL));
		CHECK(!aur_dep_satisfied("foo=1.2-2", "foo", "1.2-1", NULL));
		CHECK(aur_dep_satisfied("foo>=1:0.1", "foo", "1:0.5-1", NULL));
		CHECK(!aur_dep_satisfied("foo>=1:0.1", "foo", "9.0-1", NULL));
		CHECK(aur_dep_satisfied("bar>=1.2", "foo", "1-1", prov));
		CHECK(!aur_dep_satisfied("bar>=2", "foo", "1-1", prov));
		CHECK(aur_dep_satisfied("baz", "foo", "1-1", prov));
		CHECK(!aur_dep_satisfied("baz>=1", "foo", "1-1", prov));
		CHECK(aur_dep_satisfied("libfoo.so=2-64", "foo", "1-1", prov));
		CHECK(!aur_dep_satisfied("qux", "foo", "1-1", prov));
		alpm_list_free(prov);
		url = aur_dep_name("python>=3.11");
		CHECK_STR(url, "python");
		free(url);
	}

	/* Entpacken: Präfix entfernen, unsichere Pfade ablehnen, setuid weg */
	{
		char tmp[] = "/tmp/rcyay-test-XXXXXX", path[512], dest[512], e[256];
		const char *ok[] = { "foo/", "foo/PKGBUILD", "foo/.SRCINFO", "foo/sub/x.patch", NULL };
		const char *bad[] = { "foo/", "foo/../../boese", NULL };
		const char *abs[] = { "/etc/boese", NULL };
		struct stat st;
		CHECK(mkdtemp(tmp) != NULL);
		snprintf(path, sizeof(path), "%s/ok.tar.gz", tmp);
		snprintf(dest, sizeof(dest), "%s/out/foo", tmp);
		make_tar(path, ok);
		CHECK(aur_extract(path, dest, 1, e, sizeof(e)) == 0);
		printf("# %s\n", e);
		snprintf(path, sizeof(path), "%s/out/foo/PKGBUILD", tmp);
		CHECK(stat(path, &st) == 0 && st.st_size == 5 && !(st.st_mode & 04000));
		snprintf(path, sizeof(path), "%s/out/foo/sub/x.patch", tmp);
		CHECK(stat(path, &st) == 0);
		snprintf(path, sizeof(path), "%s/bad.tar.gz", tmp);
		make_tar(path, bad);
		CHECK(aur_extract(path, dest, 1, e, sizeof(e)) == -1 && strstr(e, "unsafe"));
		snprintf(path, sizeof(path), "%s/abs.tar.gz", tmp);
		make_tar(path, abs);
		CHECK(aur_extract(path, dest, 0, e, sizeof(e)) == -1);
		snprintf(path, sizeof(path), "%s/fehlt.tar.gz", tmp);
		CHECK(aur_extract(path, dest, 1, e, sizeof(e)) == -1);
		snprintf(path, sizeof(path), "rm -rf '%s'", tmp);
		CHECK(system(path) == 0);
	}
	return CHECK_DONE();
}

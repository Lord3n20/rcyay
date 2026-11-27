/* kernel-krypto: prüft die Kernel-Krypto-Schnittstellen, die iwd (über ell) braucht.
 * AF_ALG-Hashes/-Chiffren, keyctl DH, asymmetrische Schlüssel (X.509, PKCS#8, PKEY_QUERY).
 * Bau: gcc -static -O2 -o kernel-krypto kernel-krypto.c
 * Aufruf: kernel-krypto [ZERT.der SCHLUESSEL.pk8]   Ausgabe je Prüfung "ok"/"FEHLT", Rückgabe = Anzahl Fehler.
 * Teil von bootstrap/kernel-krypto-test (usrside, 27.11.2026). */
#include <errno.h>
#include <fcntl.h>
#include <linux/if_alg.h>
#include <linux/keyctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fehler;

static void ergebnis(const char *was, int ok, int err)
{
	printf("%-34s %s", was, ok ? "ok" : "FEHLT");
	if (!ok)
		printf(" (%s)", strerror(err));
	printf("\n");
	if (!ok)
		fehler++;
}

static int alg(const char *typ, const char *name, const void *key, size_t klen)
{
	struct sockaddr_alg sa = { .salg_family = AF_ALG };
	int s, op, e = 0;

	strncpy((char *)sa.salg_type, typ, sizeof(sa.salg_type) - 1);
	strncpy((char *)sa.salg_name, name, sizeof(sa.salg_name) - 1);
	s = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (s < 0)
		return -errno;
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		e = errno;
	else if (key && setsockopt(s, SOL_ALG, ALG_SET_KEY, key, klen) < 0)
		e = errno;
	if (e) {
		close(s);
		return -e;
	}
	op = accept(s, NULL, 0);
	e = op < 0 ? errno : 0;
	close(s);
	return op < 0 ? -e : op;
}

static void hash(const char *name, const void *key, size_t klen)
{
	char t[64];
	int op = alg("hash", name, key, klen);

	snprintf(t, sizeof(t), "hash %s", name);
	ergebnis(t, op >= 0, -op);
	if (op >= 0)
		close(op);
}

static void chiffre(const char *name, size_t klen)
{
	unsigned char key[32] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 };
	char t[64];
	int op = alg("skcipher", name, key, klen);

	snprintf(t, sizeof(t), "skcipher %s", name);
	ergebnis(t, op >= 0, -op);
	if (op >= 0)
		close(op);
}

/* Bekannte Antwort: sha256("abc") */
static void sha256_abc(void)
{
	static const unsigned char soll[32] = {
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad };
	unsigned char ist[32];
	int op = alg("hash", "sha256", NULL, 0), ok = 0;

	if (op >= 0) {
		ok = write(op, "abc", 3) == 3 && read(op, ist, 32) == 32 && !memcmp(ist, soll, 32);
		close(op);
	}
	ergebnis("sha256(\"abc\") Prüfwert", ok, op < 0 ? -op : EIO);
}

static long add_key(const char *typ, const char *desc, const void *p, size_t n)
{
	return syscall(SYS_add_key, typ, desc, p, n, KEY_SPEC_PROCESS_KEYRING);
}

static void dh(void)
{
	/* RFC 3526 MODP-Gruppe 5 (1536 Bit, Kernel-Minimum), g = 2, privat = 6 → 2^6 = 64 */
	static const unsigned char p[192] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc9, 0x0f, 0xda, 0xa2, 0x21, 0x68, 0xc2, 0x34,
		0xc4, 0xc6, 0x62, 0x8b, 0x80, 0xdc, 0x1c, 0xd1, 0x29, 0x02, 0x4e, 0x08, 0x8a, 0x67, 0xcc, 0x74,
		0x02, 0x0b, 0xbe, 0xa6, 0x3b, 0x13, 0x9b, 0x22, 0x51, 0x4a, 0x08, 0x79, 0x8e, 0x34, 0x04, 0xdd,
		0xef, 0x95, 0x19, 0xb3, 0xcd, 0x3a, 0x43, 0x1b, 0x30, 0x2b, 0x0a, 0x6d, 0xf2, 0x5f, 0x14, 0x37,
		0x4f, 0xe1, 0x35, 0x6d, 0x6d, 0x51, 0xc2, 0x45, 0xe4, 0x85, 0xb5, 0x76, 0x62, 0x5e, 0x7e, 0xc6,
		0xf4, 0x4c, 0x42, 0xe9, 0xa6, 0x37, 0xed, 0x6b, 0x0b, 0xff, 0x5c, 0xb6, 0xf4, 0x06, 0xb7, 0xed,
		0xee, 0x38, 0x6b, 0xfb, 0x5a, 0x89, 0x9f, 0xa5, 0xae, 0x9f, 0x24, 0x11, 0x7c, 0x4b, 0x1f, 0xe6,
		0x49, 0x28, 0x66, 0x51, 0xec, 0xe4, 0x5b, 0x3d, 0xc2, 0x00, 0x7c, 0xb8, 0xa1, 0x63, 0xbf, 0x05,
		0x98, 0xda, 0x48, 0x36, 0x1c, 0x55, 0xd3, 0x9a, 0x69, 0x16, 0x3f, 0xa8, 0xfd, 0x24, 0xcf, 0x5f,
		0x83, 0x65, 0x5d, 0x23, 0xdc, 0xa3, 0xad, 0x96, 0x1c, 0x62, 0xf3, 0x56, 0x20, 0x85, 0x52, 0xbb,
		0x9e, 0xd5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6d, 0x67, 0x0c, 0x35, 0x4e, 0x4a, 0xbc, 0x98, 0x04,
		0xf1, 0x74, 0x6c, 0x08, 0xca, 0x23, 0x73, 0x27, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	unsigned char g = 2, x = 6, aus[192];
	struct keyctl_dh_params par;
	long r;
	int i, ok;

	par.private = add_key("user", "dh-x", &x, 1);
	par.prime = add_key("user", "dh-p", p, sizeof(p));
	par.base = add_key("user", "dh-g", &g, 1);
	if (par.private < 0 || par.prime < 0 || par.base < 0) {
		ergebnis("keyctl user-Schlüssel", 0, errno);
		return;
	}
	memset(aus, 0xff, sizeof(aus));
	r = syscall(SYS_keyctl, KEYCTL_DH_COMPUTE, &par, aus, sizeof(aus), NULL);
	ok = r == sizeof(aus) && aus[191] == 64;
	for (i = 0; ok && i < 191; i++)
		ok = aus[i] == 0;
	ergebnis("keyctl DH_COMPUTE (2^6 mod p = 64)", ok, r < 0 ? errno : EIO);
}

static unsigned char *lesen(const char *pfad, size_t *n)
{
	static unsigned char puf[2][16384];
	static int i;
	int fd = open(pfad, O_RDONLY);
	ssize_t r;

	if (fd < 0)
		return NULL;
	r = read(fd, puf[i], sizeof(puf[i]));
	close(fd);
	if (r <= 0)
		return NULL;
	*n = r;
	return puf[i++];
}

static void asymmetrisch(const char *zert, const char *schl)
{
	size_t n;
	unsigned char *d;
	long id;
	struct keyctl_pkey_query q;

	d = lesen(zert, &n);
	id = d ? add_key("asymmetric", "test-x509", d, n) : (errno = ENOENT, -1);
	ergebnis("asymmetric X.509-Zertifikat", id >= 0, errno);
	d = lesen(schl, &n);
	id = d ? add_key("asymmetric", "test-pkcs8", d, n) : (errno = ENOENT, -1);
	ergebnis("asymmetric PKCS#8-Schlüssel", id >= 0, errno);
	if (id >= 0) {
		memset(&q, 0, sizeof(q));
		long r = syscall(SYS_keyctl, KEYCTL_PKEY_QUERY, id, 0, "enc=pkcs1 hash=sha256", &q);
		ergebnis("keyctl PKEY_QUERY (pkcs1/sha256)", r == 0 && q.key_size > 0, errno);
	}
}

int main(int argc, char **argv)
{
	const unsigned char k16[16] = "0123456789abcdef";
	static char *std[] = { "kernel-krypto", "/zert.der", "/schluessel.pk8" };

	/* als /init eines Test-Initramfs: Konsole öffnen, am Ende ausschalten */
	if (getpid() == 1) {
		mkdir("/dev", 0755);
		mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
		int fd = open("/dev/console", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
		}
		setvbuf(stdout, NULL, _IONBF, 0);
		argc = 3;
		argv = std;
	}

	sha256_abc();
	hash("md4", NULL, 0);
	hash("md5", NULL, 0);
	hash("sha1", NULL, 0);
	hash("sha256", NULL, 0);
	hash("sha384", NULL, 0);
	hash("sha512", NULL, 0);
	hash("hmac(md5)", k16, 16);
	hash("hmac(sha1)", k16, 16);
	hash("hmac(sha256)", k16, 16);
	hash("hmac(sha384)", k16, 16);
	hash("hmac(sha512)", k16, 16);
	hash("cmac(aes)", k16, 16);
	chiffre("ecb(aes)", 16);
	chiffre("cbc(aes)", 16);
	chiffre("ctr(aes)", 16);
	chiffre("ecb(des)", 8);
	chiffre("cbc(des3_ede)", 24);
	chiffre("ecb(arc4)", 16);
	dh();
	if (argc > 2)
		asymmetrisch(argv[1], argv[2]);
	printf("kernel-krypto: %d Fehler\n", fehler);
	if (getpid() == 1) {
		sync();
		reboot(RB_POWER_OFF);
	}
	return fehler;
}

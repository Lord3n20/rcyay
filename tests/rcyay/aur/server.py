#!/usr/bin/env python3
"""Mock-AUR für rcyay-Ende-zu-Ende-Tests (rcyay-e1, 26.11.2026).

Bedient aus einem Ordner PKGS/<pkgbase>/{PKGBUILD,.SRCINFO,…}:
  /rpc/v5/info?arg[]=…            /rpc/v5/search/<term>?by=name|name-desc|provides|depends|makedepends
  /cgit/aur.git/snapshot/<base>.tar.gz      /cgit/aur.git/plain/<datei>?h=<base>
  /gitlab/<proj>/-/archive/main/<proj>-main.tar.gz   /gitlab/<proj>/-/raw/main/<datei>  (aus --repo-dir)
  /pks/lookup?op=get&search=0x<FPR>          HKP-Schlüsselserver (aus --keys: <FPR>.asc)
Der Ordner wird bei jeder Anfrage neu gelesen (Tests tauschen Versionen aus).
Jede Anfrage wird in --log protokolliert.
"""
import argparse, io, json, os, sys, tarfile, time, urllib.parse
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

ARGS = None
LISTS = {"depends": "Depends", "makedepends": "MakeDepends", "checkdepends": "CheckDepends",
         "optdepends": "OptDepends", "provides": "Provides", "conflicts": "Conflicts",
         "replaces": "Replaces", "license": "License", "groups": "Groups"}


def votes(srcinfo):
    f = os.path.join(os.path.dirname(srcinfo), "votes")
    try:
        return int(open(f).read().strip())
    except (OSError, ValueError):
        return 7


def parse_srcinfo(path):
    base, pkgs, cur = {}, [], None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = (x.strip() for x in line.split("=", 1))
            if k == "pkgname":
                cur = {"pkgname": v}
                pkgs.append(cur)
                continue
            tgt = cur if cur is not None else base
            if k in LISTS or k.split("_")[0] in LISTS or k in ("arch", "source"):
                kk = k.split("_")[0] if k.split("_")[0] in LISTS else k
                tgt.setdefault(kk, [])
                if v:
                    tgt[kk].append(v)
            else:
                tgt[k] = v
    res = []
    for p in pkgs:
        ver = base.get("pkgver", "0") + "-" + base.get("pkgrel", "1")
        if base.get("epoch") and base["epoch"] != "0":
            ver = base["epoch"] + ":" + ver
        d = {"Name": p["pkgname"], "PackageBase": base["pkgbase"], "Version": ver,
             "Description": p.get("pkgdesc", base.get("pkgdesc")), "URL": p.get("url", base.get("url")),
             "URLPath": "/cgit/aur.git/snapshot/%s.tar.gz" % base["pkgbase"],
             "NumVotes": votes(path), "Popularity": votes(path) / 100.0,
             "OutOfDate": None, "Maintainer": "rcyay-test", "Submitter": "rcyay-test",
             "FirstSubmitted": 1700000000, "LastModified": int(os.path.getmtime(path)),
             "ID": abs(hash(p["pkgname"])) % 100000, "PackageBaseID": abs(hash(base["pkgbase"])) % 100000}
        for k, rk in LISTS.items():
            if k in ("makedepends", "checkdepends"):
                vals = base.get(k, [])
            else:
                vals = p.get(k, base.get(k, []))
            if vals:
                d[rk] = vals
        res.append(d)
    return res


def all_pkgs():
    out = []
    d = ARGS.pkgs
    for b in sorted(os.listdir(d)):
        si = os.path.join(d, b, ".SRCINFO")
        if os.path.isfile(si):
            try:
                out.extend(parse_srcinfo(si))
            except Exception as e:  # kaputte Testdaten sollen auffallen
                print("SRCINFO-Fehler %s: %s" % (si, e), file=sys.stderr)
    return out


def tarball(src, prefix):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        t.add(src, arcname=prefix, recursive=True,
              filter=lambda ti: None if ti.name.split("/")[-1] in ("src", "pkg") else ti)
    return buf.getvalue()


class H(BaseHTTPRequestHandler):
    def log_message(self, fmt, *a):
        if ARGS.log:
            with open(ARGS.log, "a") as f:
                f.write("%s %s\n" % (time.strftime("%H:%M:%S"), self.path))

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def rpc(self, typ, results):
        self.send(200, json.dumps({"version": 5, "type": typ, "resultcount": len(results),
                                   "results": results}))

    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(u.query)
        path = urllib.parse.unquote(u.path)
        if path in ("/rpc/v5/info", "/rpc/v5/info/"):
            names = q.get("arg[]", []) + q.get("arg", [])
            if ARGS.fail_rpc:
                return self.send(503, "kaputt", "text/plain")
            return self.rpc("multiinfo", [p for p in all_pkgs() if p["Name"] in names])
        if path.startswith("/rpc/v5/search/"):
            term = path[len("/rpc/v5/search/"):]
            by = q.get("by", ["name-desc"])[0]
            if len(term) < 2:
                return self.send(200, json.dumps({"version": 5, "type": "error", "resultcount": 0,
                                                  "results": [], "error": "Query arg too small."}))
            res = []
            for p in all_pkgs():
                if by == "name":
                    ok = term in p["Name"]
                elif by == "name-desc":
                    ok = term in p["Name"] or term in (p.get("Description") or "")
                elif by in ("provides", "depends", "makedepends", "optdepends", "checkdepends"):
                    key = {"provides": "Provides", "depends": "Depends", "makedepends": "MakeDepends",
                           "optdepends": "OptDepends", "checkdepends": "CheckDepends"}[by]
                    vals = [v.split(":")[0] for v in p.get(key, [])]
                    ok = any(v == term or v.split("=")[0].split(">")[0].split("<")[0] == term for v in vals)
                    if by == "provides" and p["Name"] == term:
                        ok = True
                else:
                    ok = False
                if ok:
                    # Suchergebnisse enthalten (wie aurweb) keine Abhängigkeitslisten
                    res.append({k: v for k, v in p.items() if k not in LISTS.values()})
            return self.rpc("search", res)
        if path.startswith("/cgit/aur.git/snapshot/") and path.endswith(".tar.gz"):
            base = path[len("/cgit/aur.git/snapshot/"):-len(".tar.gz")]
            d = os.path.join(ARGS.pkgs, base)
            if "/" in base or not os.path.isdir(d):
                return self.send(404, "not found", "text/plain")
            return self.send(200, tarball(d, base), "application/x-gzip")
        if path.startswith("/cgit/aur.git/plain/"):
            f = path[len("/cgit/aur.git/plain/"):]
            base = q.get("h", [""])[0]
            p = os.path.join(ARGS.pkgs, base, f)
            if "/" in base or ".." in f or not os.path.isfile(p):
                return self.send(404, "not found", "text/plain")
            return self.send(200, open(p, "rb").read(), "text/plain")
        if path == "/pks/lookup" and ARGS.keys:
            # HKP (draft-shaw-openpgp-hkp): gpg --keyserver hkp://127.0.0.1:PORT --recv-keys FPR
            want = q.get("search", [""])[0].upper()
            if want.startswith("0X"):
                want = want[2:]
            if q.get("op", [""])[0] == "get" and want and want.isalnum():
                for f in os.listdir(ARGS.keys):
                    if f.endswith(".asc") and f[:-4].upper().endswith(want):
                        return self.send(200, open(os.path.join(ARGS.keys, f), "rb").read(),
                                         "application/pgp-keys")
            return self.send(404, "No results found", "text/plain")
        if path.startswith("/gitlab/") and ARGS.repo_dir:
            parts = path.split("/")
            # /gitlab/<proj>/-/archive/main/<proj>-main.tar.gz  oder  /gitlab/<proj>/-/raw/main/<datei>
            if len(parts) >= 6 and parts[3] == "-":
                proj = parts[2]
                d = os.path.join(ARGS.repo_dir, proj)
                if not os.path.isdir(d):
                    return self.send(404, "not found", "text/plain")
                if parts[4] == "archive":
                    return self.send(200, tarball(d, proj + "-main-0123abcd"), "application/x-gzip")
                if parts[4] == "raw" and len(parts) == 7:
                    p = os.path.join(d, parts[6])
                    if os.path.isfile(p):
                        return self.send(200, open(p, "rb").read(), "text/plain")
            return self.send(404, "not found", "text/plain")
        return self.send(404, "not found", "text/plain")


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--pkgs", required=True)
    ap.add_argument("--repo-dir")
    ap.add_argument("--log")
    ap.add_argument("--keys")
    ap.add_argument("--fail-rpc", action="store_true")
    ARGS = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", ARGS.port), H)
    print("mock-aur bereit auf Port %d" % ARGS.port, flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()

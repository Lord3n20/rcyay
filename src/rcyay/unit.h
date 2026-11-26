/*
 *  unit.h - systemd-Units lesen und in runit/s6/BusyBox-init-Dienste übersetzen
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Reiner Übersetzer ohne Dateizugriff (Einheitstests: tests/rcyay/unit/test_unit.c).
 */
#ifndef RCYAY_UNIT_H
#define RCYAY_UNIT_H

#include <alpm_list.h>

typedef enum {
	RC_INIT_NONE = 0,   /* nicht gesetzt = wie systemd: nichts tun */
	RC_INIT_SYSTEMD,
	RC_INIT_BUSYBOX,
	RC_INIT_RUNIT,
	RC_INIT_S6
} rc_init_t;

rc_init_t rc_init_parse(const char *s);       /* "runit" → RC_INIT_RUNIT, unbekannt → -1 */
const char *rc_init_name(rc_init_t init);

typedef struct {
	char *section;
	char *key;
	char *value;
} rc_unit_kv_t;

typedef struct {
	char *name;          /* "sshd" (ohne .service), Vorlage: "dhcpcd@" */
	char *file;          /* Herkunft (nur für Kommentare/Meldungen) */
	int user;            /* aus …/systemd/user/ */
	int template;        /* NAME@.service */
	alpm_list_t *kv;     /* rc_unit_kv_t*, in Dateireihenfolge */
} rc_unit_t;

/* Parst den Text einer Unit. name = Dateiname ("sshd.service"). */
rc_unit_t *rc_unit_parse(const char *filename, const char *text, char **err);
void rc_unit_free(rc_unit_t *u);

/* letzter Wert bzw. alle Werte (leerer Wert setzt die Liste zurück wie bei systemd) */
const char *rc_unit_get(rc_unit_t *u, const char *section, const char *key);
alpm_list_t *rc_unit_all(rc_unit_t *u, const char *section, const char *key);  /* const char*, nur list_free */

enum {
	RC_SV_LONGRUN = 1,   /* überwachter Dienst: run (+ finish) */
	RC_SV_ONESHOT = 2    /* einmal beim Start: start */
};

typedef struct {
	int kind;
	char *run;           /* Langläufer: run-Skript */
	char *finish;        /* optional (forking: Dienst über PID-Datei beenden) */
	char *start;         /* Einmal-Dienst */
	alpm_list_t *needs;  /* char*: Dienste, auf die gewartet wird (Requires/BindsTo) */
} rc_svout_t;

void rc_svout_free(rc_svout_t *o);

/* Rückgabe: 0 = übersetzt (out gefüllt), 1 = bewusst übersprungen (Nutzer-Unit,
 * Vorlage, statische Unit ohne [Install]) – Grund in *why, -1 = nicht
 * übersetzbar – Grund in *why. notes: zusätzliche Hinweise (char*, malloc). */
int rc_unit_translate(rc_unit_t *u, rc_init_t init, rc_svout_t *out, char **why, alpm_list_t **notes);

#endif

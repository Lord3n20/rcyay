/* Minimaler Test-Rahmen für tests/rcyay (TAP-Ausgabe) */
#ifndef RCYAY_CHECK_H
#define RCYAY_CHECK_H
#include <stdio.h>
#include <string.h>

static int check_n = 0, check_fail = 0;

#define CHECK(cond) do { \
	check_n++; \
	if(cond) { printf("ok %d - %s\n", check_n, #cond); } \
	else { check_fail++; printf("not ok %d - %s (%s:%d)\n", check_n, #cond, __FILE__, __LINE__); } \
} while(0)

#define CHECK_STR(a, b) do { \
	const char *_a = (a), *_b = (b); \
	check_n++; \
	if(_a && _b && strcmp(_a, _b) == 0) { printf("ok %d - %s == \"%s\"\n", check_n, #a, _b); } \
	else { check_fail++; printf("not ok %d - %s: \"%s\" != \"%s\" (%s:%d)\n", check_n, #a, \
			_a ? _a : "(null)", _b ? _b : "(null)", __FILE__, __LINE__); } \
} while(0)

#define CHECK_DONE() (printf("1..%d\n", check_n), check_fail ? 1 : 0)

#endif

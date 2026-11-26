#include <stdlib.h>
#include "check.h"
#include "json.h"

static aur_json_t *P(const char *s)
{
	return aur_json_parse(s, strlen(s), NULL);
}

static int fails(const char *s)
{
	const char *err = NULL;
	aur_json_t *j = aur_json_parse(s, strlen(s), &err);
	aur_json_free(j);
	return j == NULL && err != NULL;
}

int main(void)
{
	aur_json_t *j;
	const char *doc = "{ \"type\": \"multiinfo\", \"resultcount\": 2, \"results\": ["
		"{\"Name\": \"a\\\"b\", \"N\": -1.5e2, \"T\": true, \"F\": false, \"Z\": null},"
		"{\"Name\": \"\\u00e4\\u20ac\\ud83d\\ude00\\n\\/\"}], \"empty\": {}, \"ea\": [] }";

	j = P(doc);
	CHECK(j != NULL);
	CHECK(j && j->type == AUR_JSON_OBJECT);
	CHECK_STR(aur_json_get_str(j, "type"), "multiinfo");
	CHECK(aur_json_get_num(j, "resultcount", 0) == 2);
	CHECK(aur_json_get(j, "results")->count == 2);
	CHECK_STR(aur_json_get_str(aur_json_index(aur_json_get(j, "results"), 0), "Name"), "a\"b");
	CHECK(aur_json_get_num(aur_json_index(aur_json_get(j, "results"), 0), "N", 0) == -150);
	CHECK(aur_json_get_num(aur_json_index(aur_json_get(j, "results"), 0), "T", 0) == 1);
	CHECK(aur_json_get(aur_json_index(aur_json_get(j, "results"), 0), "F")->type == AUR_JSON_BOOL);
	CHECK(aur_json_get(aur_json_index(aur_json_get(j, "results"), 0), "Z")->type == AUR_JSON_NULL);
	CHECK_STR(aur_json_get_str(aur_json_index(aur_json_get(j, "results"), 1), "Name"),
			"\xc3\xa4\xe2\x82\xac\xf0\x9f\x98\x80\n/");
	CHECK(aur_json_get(j, "empty")->type == AUR_JSON_OBJECT && aur_json_get(j, "empty")->count == 0);
	CHECK(aur_json_get(j, "ea")->type == AUR_JSON_ARRAY && aur_json_get(j, "ea")->count == 0);
	CHECK(aur_json_get(j, "missing") == NULL);
	CHECK(aur_json_get_str(j, "resultcount") == NULL);
	CHECK(aur_json_index(aur_json_get(j, "results"), 5) == NULL);
	aur_json_free(j);

	j = P("  \"x\"  ");
	CHECK(j && j->type == AUR_JSON_STRING);
	aur_json_free(j);
	j = P("42");
	CHECK(j && aur_json_num(j, 0) == 42);
	aur_json_free(j);

	CHECK(fails(""));
	CHECK(fails("{"));
	CHECK(fails("{\"a\":}"));
	CHECK(fails("{\"a\" 1}"));
	CHECK(fails("[1,]x"));
	CHECK(fails("[1 2]"));
	CHECK(fails("\"abc"));
	CHECK(fails("\"a\\q\""));
	CHECK(fails("\"a\\u12\""));
	CHECK(fails("tru"));
	CHECK(fails("{} {}"));
	CHECK(fails("\"tab\there\""));
	CHECK(fails("-"));
	{
		/* zu tief verschachtelt */
		char deep[200];
		memset(deep, '[', 100);
		deep[100] = '\0';
		CHECK(fails(deep));
	}
	/* NUL-Escape wird ersetzt, nicht abgeschnitten */
	j = P("\"a\\u0000b\"");
	CHECK(j && strlen(j->string) == 5);
	aur_json_free(j);
	return CHECK_DONE();
}

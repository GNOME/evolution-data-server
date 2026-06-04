/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

/* NB: We know which order the params will be decoded in, plain in the order they come,
 * and rfc2184 encoded following those, sorted lexigraphically */
static struct {
	const gchar *list;
	gint count;
	const gchar *params[8];
} decode_data[] = {
	{ "; charset=\"iso-8859-1\"",
	  1,
	  { "charset", "iso-8859-1" }, },
	{ "; charset=iso-8859-1",
	  1,
	  { "charset", "iso-8859-1" }, },
	{ "; charset=\"iso-8859-1\"; boundary=\"foo\"",
	  2,
	  { "charset", "iso-8859-1",
	    "boundary", "foo" }, },
	{ "; charset*1 = 8859; charset*0=\"iso-8859-1'en'iso-\";charset*2=\"-1\" ",
	  1,
	  { "charset", "iso-8859-1" }, },
	{ "; charset*1 = 8859; boundary=foo; charset*0=\"iso-8859-1'en'iso-\";charset*2=\"-1\" ",
	  2,
	  { "boundary", "foo",
	    "charset", "iso-8859-1", }, },
	{ "; charset*1 = 8859; boundary*0=f; charset*0=\"iso-8859-1'en'iso-\"; boundary*2=\"o\" ; charset*2=\"-1\"; boundary*1=o ",
	  2,
	  { "boundary", "foo",
	    "charset", "iso-8859-1", }, },
	{ "; charset*1 = 8859; boundary*0=\"iso-8859-1'en'f\"; charset*0=\"iso-8859-1'en'iso-\"; boundary*2=\"o\" ; charset*2=\"-1\"; boundary*1=o ",
	  2,
	  { "boundary", "foo",
	    "charset", "iso-8859-1", }, },
};

static struct {
	gint count;
	const gchar *params[8];
	const gchar *list;
} encode_data[] = {
	{ 1,
	  { "name", "Doul\xC3\xADk01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123457890123456789123456789" },
	  ";\n"
	  "\tname*0*=ISO-8859-1''Doul%EDk012345678901234567890123456789012345678901234;\n"
	  "\tname*1*=56789012345678901234567890123456789012345678901234567890123457890;\n"
	  "\tname*2*=123456789123456789" },
	{ 1,
	  { "name", "\"%$#@ special chars?;; !" },
	  "; name=\"\\\"%$#@ special chars?;; !\"" },
	{ 1,
	  { "name", "\"%$#@ special chars?;; !\xC3\xAD" },
	  "; name*=ISO-8859-1''%22%25$#%40%20special%20chars%3F%3B%3B%20!%ED" },
};

static void
test_param_list_decoding (void)
{
	gint i, j;

	for (i = 0; i < G_N_ELEMENTS (decode_data); i++) {
		struct _camel_header_param *head, *node;

		head = camel_header_param_list_decode (decode_data[i].list);
		g_assert_nonnull (head);
		node = head;
		for (j = 0; j < decode_data[i].count; j++) {
			if (!node)
				g_error ("param decoding[%d] '%s': didn't find all params", i, decode_data[i].list);
			g_assert_cmpstr (node->name, ==, decode_data[i].params[j * 2]);
			g_assert_cmpstr (node->value, ==, decode_data[i].params[j * 2 + 1]);
			node = node->next;
		}
		if (node != NULL)
			g_error ("param decoding[%d] '%s': found more params than should have", i, decode_data[i].list);
		camel_header_param_list_free (head);
	}
}

static void
test_param_list_encoding (void)
{
	gint i, j;

	for (i = 0; i < G_N_ELEMENTS (encode_data); i++) {
		struct _camel_header_param *head = NULL, *scan;
		gchar *text;

		for (j = 0; j < encode_data[i].count; j++)
			camel_header_set_param (&head, encode_data[i].params[j * 2], encode_data[i].params[j * 2 + 1]);
		scan = head;
		for (j = 0; scan; j++)
			scan = scan->next;
		g_assert_cmpint (j, ==, encode_data[i].count);

		text = camel_header_param_list_format (head);
		g_assert_cmpstr (text, ==, encode_data[i].list);
		camel_header_param_list_free (head);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();

	g_test_add_func ("/Camel/ParamList/Decoding", test_param_list_decoding);
	g_test_add_func ("/Camel/ParamList/Encoding", test_param_list_encoding);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

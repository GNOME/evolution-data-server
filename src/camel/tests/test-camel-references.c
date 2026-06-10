/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

struct {
	const gchar *header;
	const gchar *values[5];
} test_data[] = {
	{ "<test@camel.host>", { "test@camel.host" } },
	{ "(this is a comment) <test@camel.host>", { "test@camel.host" } },
	{ "<test@camel.host> (this is a comment)", { "test@camel.host" } },
	{ "<test@camel.host> This is total rubbish!", { "test@camel.host" } },
	{ " << test.groupwise@bug.novell>@novell>", { "test.groupwise@bug.novell" } },
	{ " << test.groupwise@bug.novell>@novell> <test@camel.host>",
	  { "test@camel.host", "test.groupwise@bug.novell" } },
	{ "<test@camel.host> <<test.groupwise@bug.novell>@novell> <test@camel.host>",
	  { "test@camel.host", "test.groupwise@bug.novell", "test@camel.host" } },
	{ " << test.groupwise@bug.novell>@novell> <test@camel.host> <<test.groupwise@bug.novell>@novell>",
	  { "test.groupwise@bug.novell", "test@camel.host", "test.groupwise@bug.novell" } },
	{ "<test@camel@host>", { "test@camel@host" } }, /* broken clients with multiple '@' */
	{ "test@camel.host", { "test@camel.host" } }, /* broken clients without <> */
	{ "test@camel@host", { "test@camel@host" } }, /* broken clients without <> */
	{ "<test@camel> <test.1.2.3@camel.1.2.3@host.3.2.1> <t.e.s.t@c.a.m.e.l>", /* mix of good and broken values */
	  { "t.e.s.t@c.a.m.e.l", "test.1.2.3@camel.1.2.3@host.3.2.1", "test@camel" } },
};

static void
test_references_decoding (void)
{
	gint i, j;

	for (i = 0; i < G_N_ELEMENTS (test_data); i++) {
		GSList *list, *link;

		list = camel_header_references_decode (test_data[i].header);
		link = list;
		for (j = 0; test_data[i].values[j]; j++) {
			if (!link)
				g_error ("references decoding[%d] '%s': didn't find all references",
					i, test_data[i].header);
			if (!string_equal (test_data[i].values[j], link->data))
				g_error ("references decoding[%d] '%s': returned ID '%s' doesn't match expected '%s'",
					i, test_data[i].header,
					(const gchar *) link->data, test_data[i].values[j]);
			link = g_slist_next (link);
		}
		if (link != NULL)
			g_error ("references decoding[%d] '%s': found more references than should have",
				i, test_data[i].header);
		g_slist_free_full (list, g_free);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/References/Decoding", test_references_decoding);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

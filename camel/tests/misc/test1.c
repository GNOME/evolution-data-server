/*
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

struct {
	const gchar *header;
	const gchar *values[5];
} test1[] = {
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
};

gint
main (gint argc,
      gchar **argv)
{
	gint i, j;

	camel_test_init (argc, argv);

	camel_test_start ("references decoding");

	for (i = 0; i < G_N_ELEMENTS (test1); i++) {
		GSList *list;

		camel_test_push ("references decoding[%d] '%s'", i, test1[i].header);
		list = camel_header_references_decode (test1[i].header);
		for (j = 0; test1[i].values[j]; j++) {
			check_msg (list != NULL, "didn't find all references");
			check (strcmp (test1[i].values[j], list->data) == 0);
			list = g_slist_next (list);
		}
		check_msg (list == NULL, "found more references than should have");
		g_slist_free_full (list, g_free);
		camel_test_pull ();
	}

	camel_test_end ();

	return 0;
}

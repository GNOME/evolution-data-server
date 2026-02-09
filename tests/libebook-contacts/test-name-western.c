/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <libebook-contacts/libebook-contacts.h>

static void
test_name_wester_parse (void)
{
	ENameWestern *nw, *copy;

	nw = e_name_western_parse ("John Doe");
	g_assert_nonnull (nw);
	g_assert_cmpstr (nw->prefix, ==, NULL);
	g_assert_cmpstr (nw->first, ==, "John");
	g_assert_cmpstr (nw->middle, ==, NULL);
	g_assert_cmpstr (nw->nick, ==, NULL);
	g_assert_cmpstr (nw->last, ==, "Doe");
	g_assert_cmpstr (nw->suffix, ==, NULL);

	copy = e_name_western_copy (nw);
	g_assert_nonnull (copy);
	g_assert_cmpstr (copy->prefix, ==, nw->prefix);
	g_assert_cmpstr (copy->first, ==, nw->first);
	g_assert_cmpstr (copy->middle, ==, nw->middle);
	g_assert_cmpstr (copy->nick, ==, nw->nick);
	g_assert_cmpstr (copy->last, ==, nw->last);
	g_assert_cmpstr (copy->suffix, ==, nw->suffix);

	e_name_western_free (copy);
	e_name_western_free (nw);

	nw = e_name_western_parse ("Miss Jane Mary Doe IV");
	g_assert_nonnull (nw);
	g_assert_cmpstr (nw->prefix, ==, "Miss");
	g_assert_cmpstr (nw->first, ==, "Jane");
	g_assert_cmpstr (nw->middle, ==, "Mary");
	g_assert_cmpstr (nw->nick, ==, NULL);
	g_assert_cmpstr (nw->last, ==, "Doe");
	g_assert_cmpstr (nw->suffix, ==, "IV");
	e_name_western_free (nw);

	nw = e_name_western_parse ("Miss Jane \"Kate\" Doe");
	g_assert_nonnull (nw);
	g_assert_cmpstr (nw->prefix, ==, "Miss");
	g_assert_cmpstr (nw->first, ==, "Jane");
	g_assert_cmpstr (nw->middle, ==, NULL);
	g_assert_cmpstr (nw->nick, ==, "\"Kate\"");
	g_assert_cmpstr (nw->last, ==, "Doe");
	g_assert_cmpstr (nw->suffix, ==, NULL);
	e_name_western_free (nw);

	nw = e_name_western_parse ("John \"Patrick\"");
	g_assert_nonnull (nw);
	g_assert_cmpstr (nw->prefix, ==, NULL);
	g_assert_cmpstr (nw->first, ==, "John");
	g_assert_cmpstr (nw->middle, ==, NULL);
	g_assert_cmpstr (nw->nick, ==, "\"Patrick\"");
	g_assert_cmpstr (nw->last, ==, NULL);
	g_assert_cmpstr (nw->suffix, ==, NULL);
	e_name_western_free (nw);

	nw = e_name_western_parse ("\"Johny\"");
	g_assert_nonnull (nw);
	g_assert_cmpstr (nw->prefix, ==, NULL);
	g_assert_cmpstr (nw->first, ==, "\"Johny\"");
	g_assert_cmpstr (nw->middle, ==, NULL);
	g_assert_cmpstr (nw->nick, ==, NULL);
	g_assert_cmpstr (nw->last, ==, NULL);
	g_assert_cmpstr (nw->suffix, ==, NULL);
	e_name_western_free (nw);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/NameWester/Parse", test_name_wester_parse);

	return g_test_run ();
}

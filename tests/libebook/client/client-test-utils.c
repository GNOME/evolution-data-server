/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc. (www.redhat.com)
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Milan Crha <mcrha@redhat.com>
 *          Matthew Barnes <mbarnes@redhat.com>
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include <libedataserver/libedataserver.h>

#include "client-test-utils.h"

void
print_email (EContact *contact)
{
	const gchar *file_as = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	const gchar *name_or_org = e_contact_get_const (contact, E_CONTACT_NAME_OR_ORG);
	GList *emails, *e;

	g_print ("   Contact: %s\n", file_as);
	g_print ("   Name or org: %s\n", name_or_org);
	g_print ("   Email addresses:\n");
	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		g_print ("\t%s\n",  (gchar *) e->data);
	}
	g_list_foreach (emails, (GFunc) g_free, NULL);
	g_list_free (emails);

	g_print ("\n");
}

gchar *
new_vcard_from_test_case (const gchar *case_name)
{
	gchar *filename;
	gchar *case_filename;
	GFile * file;
	GError *error = NULL;
	gchar *vcard;

	case_filename = g_strdup_printf ("%s.vcf", case_name);

	/* In the case of installed tests, they run in ${pkglibexecdir}/installed-tests
	 * and the vcards are installed in ${pkglibexecdir}/installed-tests/vcards
	 */
	if (g_getenv ("TEST_INSTALLED_SERVICES") != NULL)
		filename = g_build_filename (INSTALLED_TEST_DIR, "vcards", case_filename, NULL);
	else
		filename = g_build_filename (SRCDIR, "..", "data", "vcards", case_filename, NULL);

	file = g_file_new_for_path (filename);
	if (!g_file_load_contents (file, NULL, &vcard, NULL, NULL, &error)) {
		g_warning (
			"failed to read test contact file '%s': %s",
				filename, error->message);
		exit (1);
	}

	g_free (case_filename);
	g_free (filename);
	g_object_unref (file);

	return vcard;
}

static gboolean
contacts_are_equal_shallow (EContact *a,
                            EContact *b)
{
	const gchar *uid_a, *uid_b;

        /* Avoid warnings if one or more are NULL, to make this function
         * "NULL-friendly" */
	if (!a && !b)
		return TRUE;

	if (!E_IS_CONTACT (a) || !E_IS_CONTACT (b))
		return FALSE;

	uid_a = e_contact_get_const (a, E_CONTACT_UID);
	uid_b = e_contact_get_const (b, E_CONTACT_UID);

	return g_strcmp0 (uid_a, uid_b) == 0;
}

gboolean
add_contact_from_test_case_verify (EBookClient *book_client,
                                   const gchar *case_name,
                                   EContact **contact)
{
	gchar *vcard;
	EContact *contact_orig;
	EContact *contact_final;
	gchar *uid;
	GError *error = NULL;

	vcard = new_vcard_from_test_case (case_name);
	contact_orig = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	if (!e_book_client_add_contact_sync (book_client, contact_orig, &uid, NULL, &error))
		g_error ("add contact sync: %s", error->message);

	e_contact_set (contact_orig, E_CONTACT_UID, uid);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact_final, NULL, &error))
		g_error ("get contact sync: %s", error->message);

        /* verify the contact was added "successfully" (not thorough) */
	g_assert (contacts_are_equal_shallow (contact_orig, contact_final));

	if (contact)
                *contact = contact_final;
	else
		g_object_unref (contact_final);
	g_object_unref (contact_orig);
	g_free (uid);

	return TRUE;
}

gboolean
add_contact_verify (EBookClient *book_client,
                    EContact *contact)
{
	EContact *contact_final;
	gchar *uid;
	GError *error = NULL;

	if (!e_book_client_add_contact_sync (book_client, contact, &uid, NULL, &error))
		g_error ("add contact sync: %s", error->message);

	e_contact_set (contact, E_CONTACT_UID, uid);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact_final, NULL, &error))
		g_error ("get contact sync: %s", error->message);

        /* verify the contact was added "successfully" (not thorough) */
	g_assert (contacts_are_equal_shallow (contact, contact_final));

	g_object_unref (contact_final);
	g_free (uid);

	return TRUE;
}

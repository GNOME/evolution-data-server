/*
 * SPDX-FileCopyrightText: (C) 2011, 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileCopyrightText: (C) 2012 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Milan Crha <mcrha@redhat.com>
 * SPDX-FileContributor: Matthew Barnes <mbarnes@redhat.com>
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef CLIENT_TEST_UTILS_H
#define CLIENT_TEST_UTILS_H

#include <libebook/libebook.h>

void	client_test_utils_read_args	(gint argc,
					 gchar **argv);

void print_email (EContact *contact);

gboolean add_contact_from_test_case_verify (EBookClient *book_client, const gchar *case_name, EContact **contact);
gboolean add_contact_verify (EBookClient *book_client, EContact *contact);
gchar *new_vcard_from_test_case (const gchar *case_name);

#endif /* CLIENT_TEST_UTILS_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc. (www.redhat.com)
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Milan Crha <mcrha@redhat.com>
 *          Matthew Barnes <mbarnes@redhat.com>
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef CLIENT_TEST_UTILS_H
#define CLIENT_TEST_UTILS_H

#include <libebook/libebook.h>

void print_email (EContact *contact);

gboolean add_contact_from_test_case_verify (EBookClient *book_client, const gchar *case_name, EContact **contact);
gboolean add_contact_verify (EBookClient *book_client, EContact *contact);
gchar *new_vcard_from_test_case (const gchar *case_name);

#endif /* CLIENT_TEST_UTILS_H */

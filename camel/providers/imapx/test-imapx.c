/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or modify it
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
 */

#include <camel/camel.h>

gint
main (gint argc,
      gchar *argv[])
{
	CamelSession *session;
	gchar *uri = NULL;
	CamelService *service;
	CamelFolder *folder;

	if (argc != 2) {
		printf ("Pass the account url argument \n");
		return -1;
	}

	uri = argv[1];
	if (system ("rm -rf /tmp/test-camel-imapx") == -1) {
		/* no big deal */
	}
	camel_init ("/tmp/test-camel-imapx", TRUE);
	camel_provider_init ();

	session = g_object_new (
		CAMEL_TYPE_SESSION,
		"user-data-dir", "/tmp/test-camel-imapx", NULL);

	service = camel_session_add_service (
		session, "text-imapx", uri, CAMEL_PROVIDER_STORE, NULL);
	camel_service_connect_sync (service, NULL, NULL);

	camel_store_get_folder_info_sync (
		CAMEL_STORE (service), "", 3, NULL, NULL);
	folder = camel_store_get_folder_sync (
		CAMEL_STORE (service), "INBOX", 0, NULL, NULL);
	camel_folder_refresh_info_sync (folder, NULL, NULL);

	while (1)
	{
	}
	return 0;
}

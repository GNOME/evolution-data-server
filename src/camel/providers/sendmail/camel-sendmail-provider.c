/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <camel/camel.h>
#include <glib/gi18n-lib.h>

#include "camel-sendmail-transport.h"

static CamelProvider sendmail_provider = {
	.protocol = "sendmail",
	.name = N_("Sendmail"),

	.description = N_("For delivering mail by passing it to the “sendmail” program "
	   "on the local system."),

	.domain = "mail",

	.flags = 0,

	.url_flags = 0,

	.extra_conf = NULL,

	.port_entries = NULL,

	/* ... */
};

void
camel_provider_module_init (void)
{
	sendmail_provider.object_types[CAMEL_PROVIDER_TRANSPORT] =
		CAMEL_TYPE_SENDMAIL_TRANSPORT;

	sendmail_provider.translation_domain = GETTEXT_PACKAGE;

	/* Hide sendmail in Flatpak. It cannot access the host sendmail
	   anyway, neither any custom binary from the host. */
	if (!g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
		camel_provider_register (&sendmail_provider);
}


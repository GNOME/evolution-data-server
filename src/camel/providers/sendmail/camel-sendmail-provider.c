/* camel-sendmail-provider.c: sendmail provider registration code
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
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
 * Authors :
 *   Dan Winship <danw@ximian.com>
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


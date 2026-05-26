/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Chris Toshok <toshok@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-nntp-store.h"

static CamelProviderConfEntry nntp_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-all", NULL,
	  N_("Apply _filters to new messages in all folders") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "folders", NULL,
	  N_("Folders") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "short-folder-names", NULL,
	  N_("_Show folders in short notation (e.g. c.o.linux rather "
	  "than comp.os.linux)") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "folder-hierarchy-relative", NULL,
	  N_("In the subscription _dialog, show relative folder names") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "limit-latest", NULL,
	  /* Translators: The '%s' is replaced with a spin button with the actual value to use */
	  N_("Download only up to %s latest messages") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

CamelProviderPortEntry nntp_port_entries[] = {
	{ 119, N_("Default NNTP port"), FALSE },
	{ 563, N_("NNTP over TLS"), TRUE },
	{ 0, NULL, 0 }
};

static CamelProvider news_provider = {
	.protocol = "nntp",
	.name = N_("USENET news"),

	.description = N_("This is a provider for reading from and posting to "
	   "USENET newsgroups."),

	.domain = "news",

	.flags = CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL,

	.url_flags = CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_USER |
	CAMEL_URL_ALLOW_PASSWORD | CAMEL_URL_ALLOW_AUTH,

	.extra_conf = nntp_conf_entries,

	.port_entries = nntp_port_entries,

	/* ... */
};

CamelServiceAuthType camel_nntp_anonymous_authtype = {
	N_("Anonymous"),

	N_("This option will connect to the NNTP server anonymously, without "
	   "authentication."),

	"ANONYMOUS",
	FALSE
};

CamelServiceAuthType camel_nntp_password_authtype = {
	N_("Password"),

	N_("This option will authenticate with the NNTP server using a "
	   "plaintext password."),

	"PLAIN",
	TRUE
};

void
camel_provider_module_init (void)
{
	GList *auth_types;

	auth_types = g_list_append (NULL, &camel_nntp_anonymous_authtype);
	auth_types = g_list_append (auth_types, &camel_nntp_password_authtype);

	news_provider.object_types[CAMEL_PROVIDER_STORE] = camel_nntp_store_get_type ();

	news_provider.authtypes = auth_types;
	news_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register (&news_provider);
}

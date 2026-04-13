/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "camel-jmap-store.h"
#include "camel-jmap-transport.h"

static CamelProviderConfEntry jmap_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "auth", NULL,
	  N_("Authentication") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-bearer-token", NULL,
	  N_("Use _Bearer token (OAuth2) instead of password"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "bearer-token", "use-bearer-token",
	  N_("Bearer _token:"), "" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProviderPortEntry jmap_port_entries[] = {
	{ 443, N_("JMAP over HTTPS (default)"), TRUE },
	{ 80,  N_("JMAP over HTTP (insecure)"), FALSE },
	{ 0, NULL, 0 }
};

/* Provider for receiving mail (CamelStore) */
static CamelProvider jmap_provider = {
	.protocol = "jmap",

	.name = N_("JMAP"),

	.description = N_("For connecting to and synchronizing mail with JMAP servers."),

	.domain = "mail",

	.flags = CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE |
	CAMEL_PROVIDER_SUPPORTS_SSL |
	CAMEL_PROVIDER_SUPPORTS_MOBILE_DEVICES,

	.url_flags = CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST |
	CAMEL_URL_ALLOW_AUTH | CAMEL_URL_ALLOW_USER |
	CAMEL_URL_ALLOW_PASSWORD,

	.extra_conf = jmap_conf_entries,

	.port_entries = jmap_port_entries,

	/* ... */
};

/* Provider for sending mail (CamelTransport) */
static CamelProvider jmap_transport_provider = {
	.protocol = "jmap",

	.name = N_("JMAP"),

	.description = N_("For sending mail via JMAP."),

	.domain = "mail",

	.flags = CAMEL_PROVIDER_IS_REMOTE |
	CAMEL_PROVIDER_SUPPORTS_SSL,

	.url_flags = CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST |
	CAMEL_URL_ALLOW_AUTH | CAMEL_URL_ALLOW_USER |
	CAMEL_URL_ALLOW_PASSWORD,

	.extra_conf = jmap_conf_entries,

	.port_entries = jmap_port_entries,

	/* ... */
};

static CamelServiceAuthType camel_jmap_password_authtype = {
	N_("Password"),

	N_("This option will authenticate with the JMAP server "
	   "using a username and password via HTTP Basic authentication."),

	"",
	TRUE
};

static CamelServiceAuthType camel_jmap_bearer_authtype = {
	N_("Bearer Token"),

	N_("This option will authenticate with the JMAP server "
	   "using an OAuth2 Bearer token."),

	"BEARER",
	FALSE
};

void
camel_provider_module_init (void)
{
	/* Register store provider */
	jmap_provider.object_types[CAMEL_PROVIDER_STORE] = CAMEL_TYPE_JMAP_STORE;
	jmap_provider.authtypes = g_list_prepend (NULL, &camel_jmap_bearer_authtype);
	jmap_provider.authtypes = g_list_prepend (
		jmap_provider.authtypes, &camel_jmap_password_authtype);
	jmap_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register (&jmap_provider);

	/* Register transport provider */
	jmap_transport_provider.object_types[CAMEL_PROVIDER_TRANSPORT] =
		CAMEL_TYPE_JMAP_TRANSPORT;
	jmap_transport_provider.authtypes =
		g_list_prepend (NULL, &camel_jmap_bearer_authtype);
	jmap_transport_provider.authtypes = g_list_prepend (
		jmap_transport_provider.authtypes, &camel_jmap_password_authtype);
	jmap_transport_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register (&jmap_transport_provider);
}

/* camel-pop3-provider.c: pop3 provider registration code
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
 *   Michael Zucchi <notzed@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <camel/camel.h>
#include <glib/gi18n-lib.h>

#include "camel-imapx-store.h"

CamelProviderConfEntry imapx_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for New Mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check-all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check-subscribed", NULL,
	  N_("Ch_eck for new messages in subscribed folders"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-qresync", NULL,
	  N_("Use _Quick Resync if the server supports it"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-idle", NULL,
	  N_("_Listen for server change notifications"), "1" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "folders", NULL,
	  N_("Folders") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-subscriptions", NULL,
	  N_("_Show only subscribed folders"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-all", NULL,
	  N_("Apply _filters to new messages in all folders"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-inbox", "!filter-all",
	  N_("_Apply filters to new messages in Inbox on this server"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk-inbox", "filter-junk",
	  N_("Only check for Junk messages in the In_box folder"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "stay-synchronized", NULL,
	  N_("Synchroni_ze remote mail locally in all folders"), "0" },
	{ CAMEL_PROVIDER_CONF_PLACEHOLDER, "imapx-limit-by-age-placeholder", NULL },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_ADVANCED_SECTION_START, NULL, NULL, NULL },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "concurrent-connections", NULL,
	  N_("Numbe_r of concurrent connections to use"), "y:1:3:7" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "full-update-on-metered-network", NULL,
	  N_("Enable full folder update on _metered network"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "send-client-id", NULL,
	  N_("Send client I_D to the server"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-namespace", NULL,
	  N_("O_verride server-supplied folder namespace"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "namespace", "use-namespace",
	  N_("Namespace:") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "ignore-other-users-namespace", NULL,
	  N_("Ignore other users namespace"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "ignore-shared-folders-namespace", NULL,
	  N_("Ignore shared folders namespace"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-shell-command", NULL,
	  N_("Use shell command for connecting to the server"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "shell-command", "use-shell-command",
	  N_("Command:"), "" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-multi-fetch", NULL,
	  N_("Download large messages in chunks"), "0" },
	{ CAMEL_PROVIDER_CONF_OPTIONS, "fetch-order", NULL,
	  /* Translators: This constructs either "Fetch new messages in ascending order" or "Fetch new messages in descending order" */
	  N_("Fetch new messages in"), "ascending:ascending order:descending:descending order" },
	{ CAMEL_PROVIDER_CONF_HIDDEN, "$only_for_translation", NULL,
	  /* Translators: This constructs "Fetch new messages in ascending order" */
	  N_("ascending order"),
	  /* Translators: This constructs "Fetch new messages in descending order" */
	  N_("descending order") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "store-changes-interval", NULL,
	  /* Translators: The '%s' is replaced with a spin button with the actual value */
	  N_("Store folder changes after %s second(s)"), "" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "single-client-mode", NULL,
	  N_("Single client mode"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

CamelProviderPortEntry imapx_port_entries[] = {
	{ 143, N_("Default IMAP port"), FALSE },
	{ 993, N_("IMAP over TLS"), TRUE },
	{ 0, NULL, 0 }
};

static CamelProvider imapx_provider = {
	.protocol = "imapx",

	.name = N_("IMAP"),

	.description = N_("For reading and storing mail on IMAP servers."),

	.domain = "mail",

	.flags = CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL|
	CAMEL_PROVIDER_SUPPORTS_MOBILE_DEVICES |
	CAMEL_PROVIDER_SUPPORTS_BATCH_FETCH |
	CAMEL_PROVIDER_SUPPORTS_PURGE_MESSAGE_CACHE,

	.url_flags = CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH,

	.extra_conf = imapx_conf_entries,

	.port_entries = imapx_port_entries,

	/* ... */
};

extern CamelServiceAuthType camel_imapx_password_authtype;

void camel_imapx_module_init (void);

void
camel_imapx_module_init (void)
{
	imapx_provider.object_types[CAMEL_PROVIDER_STORE] =
		CAMEL_TYPE_IMAPX_STORE;
	imapx_provider.authtypes = camel_sasl_authtype_list (FALSE);
	imapx_provider.authtypes = g_list_prepend (
		imapx_provider.authtypes, &camel_imapx_password_authtype);
	imapx_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register (&imapx_provider);
}

void
camel_provider_module_init (void)
{
	camel_imapx_module_init ();
}

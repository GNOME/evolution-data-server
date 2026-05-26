/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-maildir-store.h"
#include "camel-mbox-store.h"
#include "camel-mh-store.h"
#include "camel-spool-store.h"

#define d(x)

#ifndef G_OS_WIN32

static CamelProviderConfEntry mh_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-all", NULL,
	  N_("Apply _filters to new messages in all folders") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-dot-folders", NULL,
	  N_("_Use the “.folders” folder summary file (exmh)") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider mh_provider = {
	.protocol = "mh",
	.name = N_("MH-format mail directories"),
	.description = N_("For storing local mail in MH-like mail directories."),
	.domain = "mail",
	.flags = CAMEL_PROVIDER_IS_SOURCE | CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_LOCAL,
	.url_flags = CAMEL_URL_NEED_PATH | CAMEL_URL_NEED_PATH_DIR,
	.extra_conf = mh_conf_entries,
	.port_entries = NULL,
	/* ... */
};

#endif

static CamelProviderConfEntry mbox_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-all", NULL,
	  N_("Apply _filters to new messages") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents") },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider mbox_provider = {
	.protocol = "mbox",
	.name = N_("Local delivery"),
	.description = N_("For retrieving (moving) local mail from standard mbox-formatted spools into folders managed by Evolution."),
	.domain = "mail",
	.flags = CAMEL_PROVIDER_IS_SOURCE | CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_LOCAL,
	.url_flags = CAMEL_URL_NEED_PATH,
	.extra_conf = mbox_conf_entries,  /* semi-empty entries, thus evolution will show "Receiving Options" page */
	.port_entries = NULL,
	/* ... */
};

static CamelProviderConfEntry maildir_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-inbox", NULL,
	  N_("_Apply filters to new messages in Inbox") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider maildir_provider = {
	.protocol = "maildir",
	.name = N_("Maildir-format mail directories"),
	.description = N_("For storing local mail in maildir directories."),
	.domain = "mail",
	.flags = CAMEL_PROVIDER_IS_SOURCE | CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_LOCAL,
	.url_flags = CAMEL_URL_NEED_PATH | CAMEL_URL_NEED_PATH_DIR,
	.extra_conf = maildir_conf_entries,
	.port_entries = NULL,
	/* ... */
};

#ifndef G_OS_WIN32

static CamelProviderConfEntry spool_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "listen-notifications", NULL,
	  N_("_Listen for change notifications"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-inbox", NULL,
	  N_("_Apply filters to new messages in Inbox") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-xstatus-headers", NULL, N_("_Store status headers in Elm/Pine/Mutt format") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider spool_file_provider = {
	.protocol = "spool",
	.name = N_("Standard Unix mbox spool file"),
	.description = N_("For reading and storing local mail in external standard mbox "
	"spool files.\nMay also be used to read a tree of Elm, Pine, or Mutt "
	"style folders."),
	.domain = "mail",
	.flags = CAMEL_PROVIDER_IS_SOURCE | CAMEL_PROVIDER_IS_STORAGE,
	.url_flags = CAMEL_URL_NEED_PATH,
	.extra_conf = spool_conf_entries,
	.port_entries = NULL,
	/* ... */
};

static CamelProvider spool_directory_provider = {
	.protocol = "spooldir",
	.name = N_("Standard Unix mbox spool directory"),
	.description = N_("For reading and storing local mail in external standard mbox "
	"spool files.\nMay also be used to read a tree of Elm, Pine, or Mutt "
	"style folders."),
	.domain = "mail",
	.flags = CAMEL_PROVIDER_IS_SOURCE | CAMEL_PROVIDER_IS_STORAGE,
	.url_flags = CAMEL_URL_NEED_PATH | CAMEL_URL_NEED_PATH_DIR,
	.extra_conf = spool_conf_entries,
	.port_entries = NULL,
	/* ... */
};

#endif

void
camel_provider_module_init (void)
{
	static gint init = 0;

	if (init)
		abort ();
	init = 1;

#ifndef G_OS_WIN32
	mh_conf_entries[0].value = "";  /* default path */
	mh_provider.object_types[CAMEL_PROVIDER_STORE] = camel_mh_store_get_type ();
	mh_provider.translation_domain = GETTEXT_PACKAGE;
	camel_provider_register (&mh_provider);
#endif

	mbox_provider.object_types[CAMEL_PROVIDER_STORE] = CAMEL_TYPE_MBOX_STORE;
	mbox_provider.translation_domain = GETTEXT_PACKAGE;
	camel_provider_register (&mbox_provider);

#ifndef G_OS_WIN32
	spool_file_provider.object_types[CAMEL_PROVIDER_STORE] = camel_spool_store_get_type ();
	spool_file_provider.translation_domain = GETTEXT_PACKAGE;
	camel_provider_register (&spool_file_provider);

	spool_directory_provider.object_types[CAMEL_PROVIDER_STORE] = camel_spool_store_get_type ();
	spool_directory_provider.translation_domain = GETTEXT_PACKAGE;
	camel_provider_register (&spool_directory_provider);
#endif

	maildir_provider.object_types[CAMEL_PROVIDER_STORE] = camel_maildir_store_get_type ();
	maildir_provider.translation_domain = GETTEXT_PACKAGE;
	camel_provider_register (&maildir_provider);
}

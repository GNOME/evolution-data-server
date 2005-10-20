/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
 *
 * This  program is free  software; you  can redistribute  it and/or
 * modify it under the terms of version 2  of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* Global Catalog test program */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e2k-global-catalog.h"
#include "e2k-sid.h"
#include "test-utils.h"

E2kGlobalCatalog *gc;
E2kOperation op;

static void
do_lookup (E2kGlobalCatalog *gc, const char *user)
{
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogLookupType type;
	guint32 flags;
	int i, pwd_exp_days; 
	double maxAge;

	if (*user == '/')
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN;
	else if (strchr (user, '@'))
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL;
	else
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_DN;

	flags =	E2K_GLOBAL_CATALOG_LOOKUP_SID |
		E2K_GLOBAL_CATALOG_LOOKUP_EMAIL |
		E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX |
		E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN |
		E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES |
		E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS |
		E2K_GLOBAL_CATALOG_LOOKUP_QUOTA |
		E2K_GLOBAL_CATALOG_LOOKUP_ACCOUNT_CONTROL;

	e2k_operation_init (&op);
	status = e2k_global_catalog_lookup (gc, &op, type, user, flags, &entry);
	// e2k_operation_free (&op);

	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		break;
	case E2K_GLOBAL_CATALOG_NO_SUCH_USER:
		printf ("No entry for %s\n", user);
		test_quit ();
		return;
	case E2K_GLOBAL_CATALOG_NO_DATA:
		printf ("Entry for %s contains no data\n", user);
		test_quit ();
		return;
	case E2K_GLOBAL_CATALOG_AUTH_FAILED:
		printf ("Authentication failed (try DOMAIN\\username)\n");
		test_quit ();
		return;
	default:
		printf ("Error looking up user\n");
		test_quit ();
		return;
	}

	printf ("%s (%s)\n", entry->display_name, entry->dn);
	if (entry->email)
		printf ("  email: %s\n", entry->email);
	if (entry->mailbox)
		printf ("  mailbox: %s on %s\n", entry->mailbox, entry->exchange_server);
	if (entry->legacy_exchange_dn)
		printf ("  Exchange 5.5 DN: %s\n", entry->legacy_exchange_dn);
	if (entry->sid)
		printf ("  sid: %s\n", e2k_sid_get_string_sid (entry->sid));
	if (entry->delegates) {
		printf ("  delegates:\n");
		for (i = 0; i < entry->delegates->len; i++)
			printf ("    %s\n", (char *)entry->delegates->pdata[i]);
	}
	if (entry->delegators) {
		printf ("  delegators:\n");
		for (i = 0; i < entry->delegators->len; i++)
			printf ("    %s\n", (char *)entry->delegators->pdata[i]);
	}

	if (entry->quota_warn || entry->quota_nosend || entry->quota_norecv )
		printf ("  Mail Quota Info:\n");
	if (entry->quota_warn)	
		printf ("    Issue Quota warning at : %d\n", entry->quota_warn);
	if (entry->quota_nosend)
		printf ("    Stop sending mails at  : %d\n", entry->quota_nosend);
	if (entry->quota_norecv)
		printf ("    Stop sending and recieving mails at : %d\n", entry->quota_norecv);
	if (entry->user_account_control)
		printf ("    user_account_control : %d\n", entry->user_account_control);
	

	maxAge = lookup_passwd_max_age (gc, &op);
	printf("Password max age is %f \n", maxAge);
	pwd_exp_days = (maxAge * 0.000000100)/86400 ;
	printf("Password expiery period is %d \n", pwd_exp_days);

	e2k_operation_free (&op);

	e2k_global_catalog_entry_free (gc, entry);
	test_quit ();
}

static char *
lookup_dn (E2kGlobalCatalog *gc, const char *id)
{
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogLookupType type;
	E2kGlobalCatalogStatus status;
	char *dn;

	if (id[0] == '/')
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN;
	else if (strchr (id, '@'))
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL;
	else
		return g_strdup (id);

	e2k_operation_init (&op);
	status = e2k_global_catalog_lookup (gc, &op, type, id, 0, &entry);
	e2k_operation_free (&op);

	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		break;
	case E2K_GLOBAL_CATALOG_NO_SUCH_USER:
		printf ("No entry for %s\n", id);
		exit (1);
		break;
	default:
		printf ("Error looking up user %s\n", id);
		exit (1);
		break;
	}

	dn = g_strdup (entry->dn);
	e2k_global_catalog_entry_free (gc, entry);

	return dn;
}

static void
do_modify (E2kGlobalCatalog *gc, const char *user,
	   int deleg_op, const char *delegate)
{
	char *self_dn, *deleg_dn;
	E2kGlobalCatalogStatus status;

	self_dn = lookup_dn (gc, user);
	deleg_dn = lookup_dn (gc, delegate);

	e2k_operation_init (&op);
	if (deleg_op == '+')
		status = e2k_global_catalog_add_delegate (gc, &op, self_dn, deleg_dn);
	else
		status = e2k_global_catalog_remove_delegate (gc, &op, self_dn, deleg_dn);
	e2k_operation_free (&op);

	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		printf ("Done\n");
		break;
	case E2K_GLOBAL_CATALOG_BAD_DATA:
		printf ("Invalid delegate DN\n");
		break;
	case E2K_GLOBAL_CATALOG_NO_DATA:
		printf ("No such delegate to remove\n");
		break;
	case E2K_GLOBAL_CATALOG_EXISTS:
		printf ("That user is already a delegate\n");
		break;
	default:
		printf ("Failed\n");
		break;
	}

	test_quit ();
}

static void *
cancel (void *data)
{
	e2k_operation_cancel (&op);
	return NULL;
}

static void
quit (int sig)
{
	static pthread_t cancel_thread;

	if (!cancel_thread) {
		pthread_create (&cancel_thread, NULL, cancel, NULL);
	} else
		exit (0);
}

const char *test_program_name = "gctest";

void
test_main (int argc, char **argv)
{
	const char *server;

	if (argc < 3 || argc > 4 ||
	    (argc == 4 && argv[3][0] != '+' && argv[3][0] != '-')) {
		fprintf (stderr, "Usage: %s server email-or-dn [[+|-]delegate]\n", argv[0]);
		exit (1);
	}

	signal (SIGINT, quit);

	server = argv[1];
	gc = test_get_gc (server);

	if (argc == 3)
		do_lookup (gc, argv[2]);
	else
		do_modify (gc, argv[2], argv[3][0], argv[3] + 1);

	g_object_unref (gc);
}

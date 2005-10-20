/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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

/* Change password test program */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "e2k-kerberos.h"
#include "test-utils.h"

const char *test_program_name = "cptest";

static void
krb_error (E2kKerberosResult result, const char *failed)
{
	switch (result) {
	case E2K_KERBEROS_USER_UNKNOWN:
		fprintf (stderr, "Unknown user\n");
		exit (1);

	case E2K_KERBEROS_PASSWORD_INCORRECT:
		fprintf (stderr, "Password incorrect\n");
		exit (1);

	case E2K_KERBEROS_PASSWORD_EXPIRED:
		printf ("Note: password has expired\n");
		break;

	case E2K_KERBEROS_KDC_UNREACHABLE:
		fprintf (stderr, "KDC unreachable (network problem or no such domain)\n");
		exit (1);

	case E2K_KERBEROS_TIME_SKEW:
		fprintf (stderr, "Client/server time skew is too large.\n");
		exit (1);

	case E2K_KERBEROS_PASSWORD_TOO_WEAK:
		fprintf (stderr, "Server rejected new password\n");
		exit (1);

	case E2K_KERBEROS_FAILED:
		if (failed) {
			fprintf (stderr, "%s\n", failed);
			exit (1);
		}
		/* else fall through */			

	default:
		fprintf (stderr, "Unknown error.\n");
		exit (1);
	}
}

void
test_main (int argc, char **argv)
{
	char *domain, *at, *prompt, *password;
	char *newpass1, *newpass2;
	const char *user;
	int res;

	if (argc != 2) {
		fprintf (stderr, "Usage: %s [user@]domain\n", argv[0]);
		exit (1);
	}

	domain = argv[1];
	at = strchr (domain, '@');
	if (at) {
		user = g_strndup (domain, at - domain);
		domain = at + 1;
	} else
		user = g_get_user_name ();

	prompt = g_strdup_printf ("Password for %s@%s", user, domain);
	password = test_ask_password (prompt);
	g_free (prompt);

	res = e2k_kerberos_check_password (user, domain, password);
	if (res != E2K_KERBEROS_OK)
		krb_error (res, NULL);

	newpass1 = test_ask_password ("New password");
	newpass2 = test_ask_password ("Confirm");

	if (!newpass1 || !newpass2 || strcmp (newpass1, newpass2) != 0) {
		fprintf (stderr, "Passwords do not match.\n");
		exit (1);
	}

	res = e2k_kerberos_change_password (user, domain, password, newpass1);
	if (res != E2K_KERBEROS_OK)
		krb_error (res, "Could not change password");

	printf ("Password changed\n");
	test_quit ();
}

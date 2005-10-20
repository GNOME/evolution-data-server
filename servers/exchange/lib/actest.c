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

/* Autoconfig test program */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "e2k-autoconfig.h"
#include "test-utils.h"

const char *test_program_name = "actest";

static E2kOperation op;

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

void
test_main (int argc, char **argv)
{
	E2kAutoconfig *ac;
	E2kAutoconfigResult result;
	const char *username, *password, *owa_uri, *gc_server;

	signal (SIGINT, quit);

	if (argc < 2 || argc > 4) {
		fprintf (stderr, "Usage: %s username [OWA URL] [Global Catalog server]\n", argv[0]);
		exit (1);
	}

	username = argv[1];
	password = test_get_password (username, NULL);

	owa_uri = argc > 2 ? argv[2] : NULL;
	gc_server = argc > 3 ? argv[3] : NULL;

	e2k_operation_init (&op);
	ac = e2k_autoconfig_new (owa_uri, username, password,
				 E2K_AUTOCONFIG_USE_EITHER);

	if (ac->owa_uri) {
		if (!owa_uri)
			printf ("[Default OWA URI: %s]\n", ac->owa_uri);
	} else {
		printf ("No default OWA URI available. Must specify on commandline.\n");
		goto done;
	}

	if (ac->gc_server)
		printf ("[Default GC: %s]\n", ac->gc_server);
	if (ac->nt_domain)
		printf ("[Default NT Domain: %s]\n", ac->nt_domain);
	if (ac->w2k_domain)
		printf ("[Default W2k Domain: %s]\n", ac->w2k_domain);
	printf ("\n");

	if (gc_server)
		e2k_autoconfig_set_gc_server (ac, gc_server, -1);

	result = e2k_autoconfig_check_exchange (ac, &op);
	if (result != E2K_AUTOCONFIG_OK) {
		const char *msg;
		switch (result) {
		case E2K_AUTOCONFIG_CANT_RESOLVE:
			msg = "Could not resolve hostname";
			break;
		case E2K_AUTOCONFIG_CANT_CONNECT:
			msg = "Could not connect to server";
			break;
		case E2K_AUTOCONFIG_REDIRECT:
			msg = "Multiple redirection";
			break;
		case E2K_AUTOCONFIG_AUTH_ERROR:
			msg = "Authentication error. Password incorrect?";
			break;
		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN:
			msg = "Authentication error. Password incorrect, or try DOMAIN\\username?";
			break;
		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC:
			msg = "Authentication error. Password incorrect, or try Basic auth?";
			break;
		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM:
			msg = "Authentication error. Password incorrect, or try NTLM auth?";
			break;
		case E2K_AUTOCONFIG_TRY_SSL:
			msg = "Need to use SSL";
			break;
		case E2K_AUTOCONFIG_EXCHANGE_5_5:
			msg = "This is an Exchange 5.5 server";
			break;
		case E2K_AUTOCONFIG_NOT_EXCHANGE:
			msg = "Server does not appear to be Exchange";
			break;
		case E2K_AUTOCONFIG_NO_OWA:
			msg = "Did not find OWA at given URL";
			break;
		case E2K_AUTOCONFIG_NO_MAILBOX:
			msg = "You don't seem to have a mailbox here";
			break;
		case E2K_AUTOCONFIG_CANT_BPROPFIND:
			msg = "Server does not allow BPROPFIND";
			break;
		case E2K_AUTOCONFIG_CANCELLED:
			msg = "Cancelled";
			break;
		case E2K_AUTOCONFIG_FAILED:
		default:
			msg = "Unknown error";
			break;
		}

		printf ("Exchange check to %s failed:\n  %s\n",
			ac->owa_uri, msg);
		goto done;
	}

	result = e2k_autoconfig_check_global_catalog (ac, &op);
	if (result != E2K_AUTOCONFIG_OK) {
		const char *msg;
		switch (result) {
		case E2K_AUTOCONFIG_CANT_RESOLVE:
			msg = "Could not resolve GC server";
			break;
		case E2K_AUTOCONFIG_NO_MAILBOX:
			msg = "No data for user";
			break;
		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN:
			msg = "Authentication error. Try DOMAIN\\username?";
			break;
		case E2K_AUTOCONFIG_CANCELLED:
			msg = "Cancelled";
			break;
		case E2K_AUTOCONFIG_FAILED:
		default:
			msg = "Unknown error";
			break;
		}

		printf ("\nGlobal Catalog check failed: %s\n", msg);
		if (!ac->gc_server) {
			if (ac->w2k_domain)
				printf ("got domain=%s but ", ac->w2k_domain);
			printf ("could not autodetect.\nSpecify GC on command-line.\n");
		}
		goto done;
	}

	printf ("%s is an Exchange Server %s\n\n", ac->exchange_server,
		ac->version == E2K_EXCHANGE_2000 ? "2000" :
		ac->version == E2K_EXCHANGE_2003 ? "2003" :
		"[Unknown version]");

	printf ("Name: %s\nEmail: %s\nTimezone: %s\nAccount URL: %s\n\n",
		ac->display_name, ac->email, ac->timezone, ac->account_uri);

	if (!ac->pf_server)
		printf ("Warning: public folder server was defaulted\n\n");
 done:
	e2k_operation_free (&op);
	e2k_autoconfig_free (ac);
	test_quit ();
}

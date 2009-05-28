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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Free/Busy test program. Note though that this uses the code in
 * e2k-freebusy.c, which is not currently used by Connector itself.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "e2k-freebusy.h"
#include "e2k-global-catalog.h"
#include "test-utils.h"

const gchar *test_program_name = "fbtest";

void
test_main (gint argc, gchar **argv)
{
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;
	const gchar *server, *email;
	E2kContext *ctx;
	E2kFreebusy *fb;
	E2kFreebusyEvent event;
	gint ti, bi, oi;
	gchar *public_uri;
	struct tm tm;
	time_t t;

	if (argc != 3) {
		fprintf (stderr, "Usage: %s server email-addr\n", argv[0]);
		exit (1);
	}

	server = argv[1];
	email = argv[2];

	gc = test_get_gc (server);

	status = e2k_global_catalog_lookup (
		gc, NULL, E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
		email, E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN,
		&entry);

	if (status != E2K_GLOBAL_CATALOG_OK) {
		fprintf (stderr, "Lookup failed: %d\n", status);
		test_quit ();
		return;
	}

	public_uri = g_strdup_printf ("http://%s/public", server);
	ctx = test_get_context (public_uri);
	fb = e2k_freebusy_new (ctx, public_uri, entry->legacy_exchange_dn);
	g_free (public_uri);
	g_object_unref (ctx);

	if (!fb) {
		fprintf (stderr, "Could not get fb props\n");
		test_quit ();
		return;
	}

	if (!fb->events[E2K_BUSYSTATUS_ALL]->len) {
		printf ("No data\n");
		test_quit ();
		return;
	}

	printf ("                         6am      9am      noon     3pm      6pm\n");

	ti = bi = oi = 0;
	for (t = fb->start; t < fb->end; t += 30 * 60) {
		if ((t - fb->start) % (24 * 60 * 60) == 0) {
			tm = *localtime (&t);
			printf ("\n%02d-%02d: ", tm.tm_mon + 1, tm.tm_mday);
		}

		for (; oi < fb->events[E2K_BUSYSTATUS_OOF]->len; oi++) {
			event = g_array_index (fb->events[E2K_BUSYSTATUS_OOF],
					       E2kFreebusyEvent, oi);
			if (event.end <= t)
				continue;
			if (event.start < t + (30 * 60)) {
				printf ("O");
				goto next;
			}
			if (event.start > t)
				break;
		}
		for (; bi < fb->events[E2K_BUSYSTATUS_BUSY]->len; bi++) {
			event = g_array_index (fb->events[E2K_BUSYSTATUS_BUSY],
					       E2kFreebusyEvent, bi);
			if (event.end <= t)
				continue;
			if (event.start < t + (30 * 60)) {
				printf ("X");
				goto next;
			}
			if (event.start > t)
				break;
		}
		for (; ti < fb->events[E2K_BUSYSTATUS_TENTATIVE]->len; ti++) {
			event = g_array_index (fb->events[E2K_BUSYSTATUS_TENTATIVE],
					       E2kFreebusyEvent, ti);
			if (event.end <= t)
				continue;
			if (event.start < t + (30 * 60)) {
				printf ("t");
				goto next;
			}
			if (event.start > t)
				break;
		}
		printf (".");

	next:
		if ((t - fb->start) % (60 * 60))
			printf (" ");
	}
	printf ("\n");

	test_quit ();
}

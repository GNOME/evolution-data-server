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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Generic WebDAV test program */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "e2k-context.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"

#include "test-utils.h"

static void
print_header (gpointer name, gpointer value, gpointer data)
{
	gboolean *isxml = data;

	printf ("%s: %s\n", (gchar *)name, (gchar *)value);
	if (!g_ascii_strcasecmp (name, "Content-Type") &&
	    strstr (value, "/xml"))
		*isxml = TRUE;
}

const gchar *test_program_name = "davcat";

void
test_main (gint argc, gchar **argv)
{
	E2kContext *ctx;
	SoupMessage *msg;
	GByteArray *input;
	gchar buf[1024], *base_uri, *root_uri, *eol, *vers, *p;
	gchar *method, *path, *uri;
	gchar *name, *value;
	gint nread;
	gboolean isxml = FALSE;

	if (argc != 2) {
		fprintf (stderr, "usage: %s URI\n", argv[0]);
		exit (1);
	}
	base_uri = argv[1];
	ctx = test_get_context (base_uri);

	input = g_byte_array_new ();
	do {
		nread = read (STDIN_FILENO, buf, sizeof (buf));
		if (nread > 0)
			g_byte_array_append (input, buf, nread);
	} while (nread > 0 || (nread == -1 && errno == EINTR));
	g_byte_array_append (input, "", 1);

	method = input->data;
	eol = strchr (method, '\n');
	p = strchr (method, ' ');
	if (!eol || !p || p > eol) {
		fprintf (stderr, "Could not parse request method\n");
		exit (1);
	}
	*p = '\0';

	path = p + 1;
	if (*path == '/')
		path++;
	p = strchr (path, ' ');
	if (!p || p > eol)
		p = eol;
	*p = '\0';
	if (p < eol)
		vers = p + 1;
	else
		vers = NULL;

	root_uri = g_strdup (base_uri);
	p = strstr (root_uri, "://");
	if (p) {
		p = strchr (p + 3, '/');
		if (p)
			*p = '\0';
	}
	uri = e2k_uri_concat (root_uri, path);
	g_free (root_uri);
	msg = e2k_soup_message_new (ctx, uri, method);
	if (!msg) {
		fprintf (stderr, "Could not create message to %s\n", uri);
		exit (1);
	}
	g_free (uri);

	if (vers) {
		if (strncmp (vers, "HTTP/1.", 7) != 0 ||
		    (vers[7] != '0' && vers[7] != '1')) {
			fprintf (stderr, "Could not parse HTTP version\n");
			exit (1);
		}
		if (vers[7] == '0')
			soup_message_set_http_version (msg, SOUP_HTTP_1_0);
	}

	while (1) {
		name = eol + 1;
		eol = strchr (name, '\n');
		p = strchr (name, ':');
		if (!eol || eol == name || !p || p > eol || p[1] != ' ')
			break;
		*p = '\0';
		value = p + 2;
		*eol = '\0';
		if (eol[-1] == '\r')
			eol[-1] = '\0';
		soup_message_add_header (msg->request_headers, name, value);
	}

	p = name;
	if (*p == '\r')
		p++;
	if (*p == '\n')
		p++;

	if (*p) {
		msg->request.body = e2k_lf_to_crlf (p);
		msg->request.length = strlen (msg->request.body);
		msg->request.owner = SOUP_BUFFER_SYSTEM_OWNED;

		if (!soup_message_get_header (msg->request_headers, "Content-Type")) {
			soup_message_add_header (msg->request_headers,
						 "Content-Type", "text/xml");
		}
	}

	e2k_context_send_message (ctx, NULL, msg);

	printf ("%d %s\n", msg->status_code, msg->reason_phrase);
	if (SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code))
		exit (1);

	soup_message_foreach_header (msg->response_headers,
				     print_header, &isxml);
	printf ("\n");

	if (isxml) {
		xmlDoc *doc;

		doc = e2k_parse_xml (msg->response.body, msg->response.length);
		if (doc) {
			xmlDocFormatDump (stdout, doc, 1);
			xmlFreeDoc (doc);
		} else
			fwrite (msg->response.body, 1, msg->response.length, stdout);
	} else
		fwrite (msg->response.body, 1, msg->response.length, stdout);
	printf ("\n");

	g_object_unref (msg);
	g_byte_array_free (input, TRUE);
	g_object_unref (ctx);
	test_quit ();
}

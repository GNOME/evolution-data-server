/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
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

/* WebDAV test program / utility */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libsoup/soup-misc.h>

#include "e2k-context.h"
#include "e2k-restriction.h"
#include "e2k-security-descriptor.h"
#include "e2k-sid.h"
#include "e2k-xml-utils.h"

#include "e2k-propnames.h"
#include "e2k-propnames.c"

#include "test-utils.h"

static E2kContext *ctx;
static E2kOperation op;

static const gchar *folder_tree_props[] = {
	E2K_PR_DAV_DISPLAY_NAME,
	E2K_PR_EXCHANGE_FOLDER_CLASS
};
static const gint n_folder_tree_props = sizeof (folder_tree_props) / sizeof (folder_tree_props[0]);

static void
display_folder_tree (E2kContext *ctx, gchar *top)
{
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	gint status;
	const gchar *name, *class;

	e2k_operation_init (&op);
	rn = e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					E2K_RELOP_EQ, TRUE);
	iter = e2k_context_search_start (ctx, &op, top,
					 folder_tree_props,
					 n_folder_tree_props,
					 rn, NULL, TRUE);
	e2k_restriction_unref (rn);

	while ((result = e2k_result_iter_next (iter))) {
		name = e2k_properties_get_prop (result->props,
						E2K_PR_DAV_DISPLAY_NAME);
		class = e2k_properties_get_prop (result->props,
						 E2K_PR_EXCHANGE_FOLDER_CLASS);

		printf ("%s:\n    %s, %s\n", result->href,
			name, class ? class : "(No Outlook folder class)");
	}
	status = e2k_result_iter_free (iter);
	e2k_operation_free (&op);

	test_abort_if_http_error (status);
	test_quit ();
}

static void
list_contents (E2kContext *ctx, gchar *top, gboolean reverse)
{
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	const gchar *prop;
	gint status;

	e2k_operation_init (&op);
	prop = E2K_PR_DAV_DISPLAY_NAME;
	rn = e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					E2K_RELOP_EQ, FALSE);
	iter = e2k_context_search_start (ctx, &op, top, &prop, 1,
					 rn, NULL, !reverse);
	e2k_restriction_unref (rn);

	while ((result = e2k_result_iter_next (iter))) {
		printf ("%3d %s (%s)\n", e2k_result_iter_get_index (iter),
			result->href,
			(gchar *)e2k_properties_get_prop (result->props,
							 E2K_PR_DAV_DISPLAY_NAME));
	}
	status = e2k_result_iter_free (iter);
	e2k_operation_free (&op);

	test_abort_if_http_error (status);
	test_quit ();
}

static gint
mp_compar (gconstpointer k, gconstpointer m)
{
	const gchar *key = k;
	struct mapi_proptag *mp = (gpointer)m;

	return strncmp (key, mp->proptag, 5);
}

static void
print_propname (const gchar *propname)
{
	struct mapi_proptag *mp;

	printf ("  %s", propname);

	if (!strncmp (propname, E2K_NS_MAPI_PROPTAG, sizeof (E2K_NS_MAPI_PROPTAG) - 1)) {
		mp = bsearch (propname + 42, mapi_proptags, nmapi_proptags,
			      sizeof (struct mapi_proptag), mp_compar);
		if (mp)
			printf (" (%s)", mp->name);
	}

	printf (":\n");
}

static void
print_binary (GByteArray *data)
{
	guchar *start, *end, *p;

	end = data->data + data->len;
	for (start = data->data; start < end; start += 16) {
		printf ("    ");
		for (p = start; p < end && p < start + 16; p++)
			printf ("%02x ", *p);
		while (p++ < start + 16)
			printf ("   ");
		printf ("   ");
		for (p = start; p < end && p < start + 16; p++)
			printf ("%c", isprint (*p) ? *p : '.');
		printf ("\n");
	}
}

typedef struct {
	const gchar *propname;
	E2kPropType type;
	gpointer value;
} EBrowseProp;

static gint
prop_compar (gconstpointer a, gconstpointer b)
{
	EBrowseProp **pa = (gpointer)a;
	EBrowseProp **pb = (gpointer)b;

	return strcmp ((*pa)->propname, (*pb)->propname);
}

static void
print_prop (EBrowseProp *prop)
{
	print_propname (prop->propname);

	switch (prop->type) {
	case E2K_PROP_TYPE_BINARY:
		print_binary (prop->value);
		break;

	case E2K_PROP_TYPE_STRING_ARRAY:
	case E2K_PROP_TYPE_INT_ARRAY:
	{
		GPtrArray *array = prop->value;
		gint i;

		for (i = 0; i < array->len; i++)
			printf ("    %s\n", (gchar *)array->pdata[i]);
		break;
	}

	case E2K_PROP_TYPE_BINARY_ARRAY:
	{
		GPtrArray *array = prop->value;
		gint i;

		for (i = 0; i < array->len; i++) {
			print_binary (array->pdata[i]);
			printf ("\n");
		}
		break;
	}

	case E2K_PROP_TYPE_XML:
		printf ("    (xml)\n");
		break;

	case E2K_PROP_TYPE_STRING:
	default:
		printf ("    %s\n", (gchar *)prop->value);
		break;
	}
}

static void
add_prop (const gchar *propname, E2kPropType type, gpointer value, gpointer props)
{
	EBrowseProp *prop;

	prop = g_new0 (EBrowseProp, 1);
	prop->propname = propname;
	prop->type = type;
	prop->value = value;
	g_ptr_array_add (props, prop);
}

static void
print_properties (E2kResult *results, gint nresults)
{
	GPtrArray *props;
	gint i;

	if (nresults != 1) {
		printf ("Got %d results?\n", nresults);
		test_quit ();
		return;
	}

	printf ("%s\n", results[0].href);
	props = g_ptr_array_new ();
	e2k_properties_foreach (results[0].props, add_prop, props);
	qsort (props->pdata, props->len, sizeof (gpointer), prop_compar);

	for (i = 0; i < props->len; i++)
		print_prop (props->pdata[i]);

	test_quit ();
}

static void
got_all_properties (SoupMessage *msg, gpointer ctx)
{
	E2kResult *results;
	gint nresults;

	test_abort_if_http_error (msg->status_code);

	e2k_results_from_multistatus (msg, &results, &nresults);
	test_abort_if_http_error (msg->status_code);
	print_properties (results, nresults);
	e2k_results_free (results, nresults);
}

#define ALL_PROPS \
"<?xml version=\"1.0\" encoding=\"utf-8\" ?>" \
"<propfind xmlns=\"DAV:\" xmlns:e=\"http://schemas.microsoft.com/exchange/\">" \
"  <allprop>" \
"    <e:allprop/>" \
"  </allprop>" \
"</propfind>"

static void
get_all_properties (E2kContext *ctx, gchar *uri)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new_full (ctx, uri, "PROPFIND",
					 "text/xml", SOUP_BUFFER_USER_OWNED,
					 ALL_PROPS, strlen (ALL_PROPS));
	soup_message_add_header (msg->request_headers, "Brief", "t");
	soup_message_add_header (msg->request_headers, "Depth", "0");

	e2k_context_queue_message (ctx, msg, got_all_properties, ctx);
}

static void
get_property (E2kContext *ctx, gchar *uri, gchar *prop)
{
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults, i;

	if (!strncmp (prop, "PR_", 3)) {
		for (i = 0; i < nmapi_proptags; i++)
			if (!strcmp (mapi_proptags[i].name, prop)) {
				prop = g_strconcat (E2K_NS_MAPI_PROPTAG,
						    mapi_proptags[i].proptag,
						    NULL);
				break;
			}
	}

	e2k_operation_init (&op);
	status = e2k_context_propfind (ctx, &op, uri,
				       (const gchar **)&prop, 1,
				       &results, &nresults);
	e2k_operation_free (&op);
	test_abort_if_http_error (status);
	print_properties (results, nresults);
	e2k_results_free (results, nresults);
}

static void
get_fav_properties(E2kContext *ctx, gchar *uri)
{
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	const gchar *prop;
	gint status;
	gchar *eml_str, *top = uri, fav_uri[1024];

	/* list the contents and search for the favorite properties */
	e2k_operation_init (&op);
	prop = E2K_PR_DAV_DISPLAY_NAME;
	rn = e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					E2K_RELOP_EQ, FALSE);
	iter = e2k_context_search_start (ctx, &op, top, &prop, 1,
                                         rn, NULL, FALSE);
	e2k_restriction_unref (rn);

	while ((result = e2k_result_iter_next (iter))) {
		strcpy(fav_uri, uri);
		eml_str = strstr(result->href, "Shortcuts");
		eml_str = eml_str + strlen("Shortcuts");

		strcat(fav_uri, eml_str);

		printf("\nNAME:\n");
		get_property (ctx, fav_uri, PR_FAV_DISPLAY_NAME);
		printf("\nALIAS:\n");
		get_property (ctx, fav_uri, PR_FAV_DISPLAY_ALIAS);
		printf("\nPUBLIC SOURCE KEY:\n");
		get_property (ctx, fav_uri, PR_FAV_PUBLIC_SOURCE_KEY);
		printf("\nPARENT SOURCE KEY:\n");
		get_property (ctx, fav_uri, PR_FAV_PARENT_SOURCE_KEY);
		printf("\nAUTO SUBFOLDERS:\n");
		get_property (ctx, fav_uri, PR_FAV_AUTOSUBFOLDERS);
		printf("\nLEVEL MASK:\n");
		get_property (ctx, fav_uri, PR_FAV_LEVEL_MASK);
		printf("\nINHERIT AUTO:\n");
		get_property (ctx, fav_uri, PR_FAV_INHERIT_AUTO);
		printf("\nDEL SUBS:\n");
		get_property (ctx, fav_uri, PR_FAV_DEL_SUBS);
		printf("\n\t\t=================================================\n");

		memset(fav_uri, 0, 1024);
	}
	status = e2k_result_iter_free (iter);
	e2k_operation_free (&op);

	test_abort_if_http_error (status);
	test_quit ();
}

static void
get_sd (E2kContext *ctx, gchar *uri)
{
	const gchar *props[] = {
		E2K_PR_EXCHANGE_SD_BINARY,
		E2K_PR_EXCHANGE_SD_XML,
	};
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults;
	xmlNodePtr xml_form;
	GByteArray *binary_form;
	E2kSecurityDescriptor *sd;
	E2kPermissionsRole role;
	guint32 perms;
	GList *sids, *s;
	E2kSid *sid;

	e2k_operation_init (&op);
	status = e2k_context_propfind (ctx, &op, uri, props, 2,
				       &results, &nresults);
	e2k_operation_free (&op);
	test_abort_if_http_error (status);

	if (nresults == 0)
		goto done;

	xml_form = e2k_properties_get_prop (results[0].props,
					    E2K_PR_EXCHANGE_SD_XML);
	binary_form = e2k_properties_get_prop (results[0].props,
					       E2K_PR_EXCHANGE_SD_BINARY);
	if (!xml_form || !binary_form)
		goto done;

	xmlElemDump (stdout, NULL, xml_form);
	printf ("\n");

	print_binary (binary_form);
	printf ("\n");

	sd = e2k_security_descriptor_new (xml_form, binary_form);
	if (!sd) {
		printf ("(Could not parse)\n");
		goto done;
	}

	sids = e2k_security_descriptor_get_sids (sd);
	for (s = sids; s; s = s->next) {
		sid = s->data;
		perms = e2k_security_descriptor_get_permissions (sd, sid);
		role = e2k_permissions_role_find (perms);
		printf ("%s: %s (0x%lx)\n",
			e2k_sid_get_display_name (sid),
			e2k_permissions_role_get_name (role),
			(gulong)perms);
	}
	g_list_free (sids);

	if (!e2k_security_descriptor_to_binary (sd))
		printf ("\nSD is malformed.\n");
	g_object_unref (sd);

 done:
	test_quit ();
}

static void
get_body (E2kContext *ctx, gchar *uri)
{
	E2kHTTPStatus status;
	gchar *body;
	gint len;

	e2k_operation_init (&op);
	status = e2k_context_get (ctx, &op, uri, NULL, &body, &len);
	e2k_operation_free (&op);
	test_abort_if_http_error (status);

	fwrite (body, 1, len, stdout);
	test_quit ();
}

static void
delete (E2kContext *ctx, gchar *uri)
{
	E2kHTTPStatus status;

	e2k_operation_init (&op);
	status = e2k_context_delete (ctx, &op, uri);
	e2k_operation_free (&op);
	test_abort_if_http_error (status);
	test_quit ();
}

static void
notify (E2kContext *ctx, const gchar *uri,
	E2kContextChangeType type, gpointer user_data)
{
	switch (type) {
	case E2K_CONTEXT_OBJECT_CHANGED:
		printf ("Changed\n");
		break;
	case E2K_CONTEXT_OBJECT_ADDED:
		printf ("Added\n");
		break;
	case E2K_CONTEXT_OBJECT_REMOVED:
		printf ("Removed\n");
		break;
	case E2K_CONTEXT_OBJECT_MOVED:
		printf ("Moved\n");
		break;
	}
}

static void
subscribe (E2kContext *ctx, gchar *uri)
{
	e2k_context_subscribe (ctx, uri,
			       E2K_CONTEXT_OBJECT_CHANGED, 0,
			       notify, NULL);
	e2k_context_subscribe (ctx, uri,
			       E2K_CONTEXT_OBJECT_ADDED, 0,
			       notify, NULL);
	e2k_context_subscribe (ctx, uri,
			       E2K_CONTEXT_OBJECT_REMOVED, 0,
			       notify, NULL);
	e2k_context_subscribe (ctx, uri,
			       E2K_CONTEXT_OBJECT_MOVED, 0,
			       notify, NULL);
}

static void
move (E2kContext *ctx, gchar *from, gchar *to, gboolean delete)
{
	GPtrArray *source_hrefs;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;

	source_hrefs = g_ptr_array_new ();
	g_ptr_array_add (source_hrefs, "");

	e2k_operation_init (&op);
	iter = e2k_context_transfer_start (ctx, &op, from, to,
					   source_hrefs, delete);
	g_ptr_array_free (source_hrefs, TRUE);

	result = e2k_result_iter_next (iter);
	if (result) {
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (result->status))
			printf ("Failed: %d\n", result->status);
		else {
			printf ("moved to %s\n",
				(gchar *)e2k_properties_get_prop (result->props,
								 E2K_PR_DAV_LOCATION));
		}
	}
	status = e2k_result_iter_free (iter);
	e2k_operation_free (&op);

	test_abort_if_http_error (status);
	test_quit ();
}

static void
name (E2kContext *ctx, gchar *alias, gchar *uri_prefix)
{
	E2kHTTPStatus status;
	gchar *uri, *body;
	gint len;
	xmlDoc *doc;
	xmlNode *item, *node;
	gchar *data;

	uri = g_strdup_printf ("%s?Cmd=galfind&AN=%s", uri_prefix, alias);
	e2k_operation_init (&op);
	status = e2k_context_get_owa (ctx, &op, uri, TRUE, &body, &len);
	e2k_operation_free (&op);
	test_abort_if_http_error (status);

	doc = e2k_parse_xml (body, len);

	if ((node = e2k_xml_find (doc->children, "error")))
		printf ("Error: %s\n", xmlNodeGetContent (node));
	else {
		item = doc->children;
		while ((item = e2k_xml_find (item, "item"))) {
			for (node = item->children; node; node = node->next) {
				if (node->type == XML_ELEMENT_NODE) {
					data = xmlNodeGetContent (node);
					if (data && *data)
						printf ("%s: %s\n", node->name, data);
					xmlFree (data);
				}
			}
		}
	}

	xmlFreeDoc (doc);
	test_quit ();
}

static void
put (E2kContext *ctx, const gchar *file, const gchar *uri)
{
	struct stat st;
	gchar *buf;
	gint fd;
	E2kHTTPStatus status;

	fd = open (file, O_RDONLY);
	if (fd == -1 || fstat (fd, &st) == -1) {
		fprintf (stderr, "%s\n", g_strerror (errno));
		exit (1);
	}
	buf = g_malloc (st.st_size);
	read (fd, buf, st.st_size);
	close (fd);

	e2k_operation_init (&op);
	status = e2k_context_put (ctx, &op, uri,
				  "message/rfc822", buf, st.st_size,
				  NULL);
	e2k_operation_free (&op);
	test_abort_if_http_error (status);
	test_quit ();
}

static gpointer
cancel (gpointer op)
{
	e2k_operation_cancel (op);
	return NULL;
}

static void
quit (gint sig)
{
	static pthread_t cancel_thread;

	/* Can't cancel from here because we might be
	 * inside a malloc.
	 */
	if (!cancel_thread) {
		pthread_create (&cancel_thread, NULL, cancel, &op);
	} else
		exit (0);
}

static void
usage (void)
{
	printf ("usage: ebrowse -t URI                       (shallow folder tree)\n");
	printf ("       ebrowse [-l | -L ] URI               (contents listing [back/forward])\n");
	printf ("       ebrowse [ -p | -P prop ] URI         (look up all/one prop)\n");
	printf ("       ebrowse -S URI                       (look up security descriptor)\n");
	printf ("       ebrowse -b URI                       (fetch body)\n");
	printf ("       ebrowse -q FILE URI                  (put body)\n");
	printf ("       ebrowse -d URI                       (delete)\n");
	printf ("       ebrowse -s URI                       (subscribe and listen)\n");
	printf ("       ebrowse [ -m | -c ] SRC DEST         (move/copy)\n");
	printf ("       ebrowse -n ALIAS URI                 (lookup name)\n");
	printf ("       ebrowse -f URI			     (lookup favorite folder props)\n");
	exit (1);
}

const gchar *test_program_name = "ebrowse";

void
test_main (gint argc, gchar **argv)
{
	gchar *uri;

	signal (SIGINT, quit);

	uri = argv[argc - 1];
	ctx = test_get_context (uri);

	switch (argv[1][1]) {
	case 't':
		display_folder_tree (ctx, uri);
		break;

	case 'l':
		list_contents (ctx, uri, FALSE);
		break;

	case 'L':
		list_contents (ctx, uri, TRUE);
		break;

	case 'b':
		get_body (ctx, uri);
		break;

	case 'd':
		delete (ctx, uri);
		break;

	case 'p':
		get_all_properties (ctx, uri);
		break;

	case 'P':
		get_property (ctx, uri, argv[2]);
		break;

	case 'S':
		get_sd (ctx, uri);
		break;

	case 's':
		subscribe (ctx, uri);
		break;

	case 'm':
	case 'c':
		move (ctx, argv[2], uri, argv[1][1] == 'm');
		break;

	case 'n':
		name (ctx, argv[2], uri);
		break;

	case 'q':
		put (ctx, argv[2], uri);
		break;

	case 'f':
		get_fav_properties(ctx, uri);
		break;

	default:
		usage ();
	}
}

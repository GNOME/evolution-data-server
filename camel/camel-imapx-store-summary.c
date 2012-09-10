/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <camel/camel-file-utils.h>
#include "camel-imapx-utils.h"
#include "camel-imapx-store-summary.h"

#define d(...) camel_imapx_debug(debug, '?', __VA_ARGS__)

#define CAMEL_IMAPX_STORE_SUMMARY_VERSION_0 (0)

#define CAMEL_IMAPX_STORE_SUMMARY_VERSION (0)

static gint summary_header_load (CamelStoreSummary *, FILE *);
static gint summary_header_save (CamelStoreSummary *, FILE *);

/*static CamelStoreInfo * store_info_new(CamelStoreSummary *, const gchar *);*/
static CamelStoreInfo * store_info_load (CamelStoreSummary *, FILE *);
static gint		 store_info_save (CamelStoreSummary *, FILE *, CamelStoreInfo *);
static void		 store_info_free (CamelStoreSummary *, CamelStoreInfo *);

static const gchar *store_info_string (CamelStoreSummary *, const CamelStoreInfo *, gint);
static void store_info_set_string (CamelStoreSummary *, CamelStoreInfo *, int, const gchar *);

G_DEFINE_TYPE (CamelIMAPXStoreSummary, camel_imapx_store_summary, CAMEL_TYPE_STORE_SUMMARY)

static void
imapx_store_summary_finalize (GObject *object)
{
	CamelIMAPXStoreSummary *summary;

	summary = CAMEL_IMAPX_STORE_SUMMARY (object);

	camel_imapx_namespace_list_clear (summary->namespaces);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_store_summary_parent_class)->finalize (object);
}

static void
camel_imapx_store_summary_class_init (CamelIMAPXStoreSummaryClass *class)
{
	GObjectClass *object_class;
	CamelStoreSummaryClass *store_summary_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = imapx_store_summary_finalize;

	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (class);
	store_summary_class->summary_header_load = summary_header_load;
	store_summary_class->summary_header_save = summary_header_save;
	store_summary_class->store_info_load = store_info_load;
	store_summary_class->store_info_save = store_info_save;
	store_summary_class->store_info_free = store_info_free;
	store_summary_class->store_info_string = store_info_string;
	store_summary_class->store_info_set_string = store_info_set_string;
}

static void
camel_imapx_store_summary_init (CamelIMAPXStoreSummary *s)
{
	((CamelStoreSummary *) s)->store_info_size = sizeof (CamelIMAPXStoreInfo);
	s->version = CAMEL_IMAPX_STORE_SUMMARY_VERSION;
}

/**
 * camel_imapx_store_summary_new:
 *
 * Create a new CamelIMAPXStoreSummary object.
 *
 * Returns: A new CamelIMAPXStoreSummary widget.
 **/
CamelIMAPXStoreSummary *
camel_imapx_store_summary_new (void)
{
	return g_object_new (CAMEL_TYPE_IMAPX_STORE_SUMMARY, NULL);
}

/**
 * camel_imapx_store_summary_full_name:
 * @s:
 * @full_name:
 *
 * Retrieve a summary item by full name.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 *
 * Returns: The summary item, or NULL if the @full_name name
 * is not available.
 * It must be freed using camel_store_summary_info_free().
 **/
CamelIMAPXStoreInfo *
camel_imapx_store_summary_full_name (CamelIMAPXStoreSummary *s,
                                     const gchar *full_name)
{
	gint count, i;
	CamelIMAPXStoreInfo *info;
	gboolean is_inbox = g_ascii_strcasecmp (full_name, "INBOX") == 0;

	count = camel_store_summary_count ((CamelStoreSummary *) s);
	for (i = 0; i < count; i++) {
		info = (CamelIMAPXStoreInfo *) camel_store_summary_index ((CamelStoreSummary *) s, i);
		if (info) {
			if (strcmp (info->full_name, full_name) == 0 ||
			    (is_inbox && g_ascii_strcasecmp (info->full_name, full_name) == 0))
				return info;
			camel_store_summary_info_free ((CamelStoreSummary *) s, (CamelStoreInfo *) info);
		}
	}

	return NULL;
}

gchar *
camel_imapx_store_summary_full_to_path (CamelIMAPXStoreSummary *s,
                                        const gchar *full_name,
                                        gchar dir_sep)
{
	gchar *path, *p;

	p = path = g_strdup (full_name);

	if (dir_sep && dir_sep != '/') {
		while (*p) {
			if (*p == '/')
				*p = dir_sep;
			else if (*p == dir_sep)
				*p = '/';
			p++;
		}
	}
	return path;
}

gchar *
camel_imapx_store_summary_path_to_full (CamelIMAPXStoreSummary *s,
                                        const gchar *path,
                                        gchar dir_sep)
{
	gchar *full, *f;
	const gchar *p;
	gchar *subpath, *last = NULL;
	CamelStoreInfo *si;
	CamelIMAPXStoreNamespace *ns;

	/* check to see if we have a subpath of path already defined */
	subpath = alloca (strlen (path) + 1);
	strcpy (subpath, path);
	do {
		si = camel_store_summary_path ((CamelStoreSummary *) s, subpath);
		if (si == NULL) {
			last = strrchr (subpath, '/');
			if (last)
				*last = 0;
		}
	} while (si == NULL && last);

	/* path is already present, use the raw version we have */
	if (si && strlen (subpath) == strlen (path)) {
		f = g_strdup (camel_imapx_store_info_full_name (s, si));
		camel_store_summary_info_free ((CamelStoreSummary *) s, si);
		return f;
	}

	ns = camel_imapx_store_summary_namespace_find_path (s, path);

	if (si)
		p = path + strlen (subpath);
	else if (ns)
		p = path + strlen (ns->path);
	else
		p = path;

	f = full = g_strdup (p);
	if (dir_sep != '/') {
		while (*f) {
			if (*f == '/')
				*f = dir_sep;
			else if (*f == dir_sep)
				*f = '/';
			f++;
		}
	}

	/* merge old path part if required */
	f = full;
	if (si) {
		full = g_strdup_printf ("%s%s", camel_imapx_store_info_full_name (s, si), f);
		g_free (f);
		camel_store_summary_info_free ((CamelStoreSummary *) s, si);
		f = full;
	} else if (ns) {
		full = g_strdup_printf ("%s%s", ns->full_name, f);
		g_free (f);
		f = full;
	}

	return f;
}

CamelIMAPXStoreInfo *
camel_imapx_store_summary_add_from_full (CamelIMAPXStoreSummary *s,
                                         const gchar *full,
                                         gchar dir_sep)
{
	CamelIMAPXStoreInfo *info;
	gchar *pathu8, *prefix;
	gint len;
	gchar *full_name;
	CamelIMAPXStoreNamespace *ns;

	d ("adding full name '%s' '%c'\n", full, dir_sep);

	len = strlen (full);
	full_name = alloca (len + 1);
	strcpy (full_name, full);
	if (full_name[len - 1] == dir_sep)
		full_name[len - 1] = 0;

	info = camel_imapx_store_summary_full_name (s, full_name);
	if (info) {
		camel_store_summary_info_free ((CamelStoreSummary *) s, (CamelStoreInfo *) info);
		d ("  already there\n");
		return info;
	}

	ns = camel_imapx_store_summary_namespace_find_full (s, full_name);
	if (ns) {
		d ("(found namespace for '%s' ns '%s') ", full_name, ns->path);
		len = strlen (ns->full_name);
		if (len >= strlen (full_name)) {
			pathu8 = g_strdup (ns->path);
		} else {
			if (full_name[len] == ns->sep)
				len++;

			prefix = camel_imapx_store_summary_full_to_path (s, full_name + len, ns->sep);
			if (*ns->path) {
				pathu8 = g_strdup_printf ("%s/%s", ns->path, prefix);
				g_free (prefix);
			} else {
				pathu8 = prefix;
			}
		}
		d (" (pathu8 = '%s')", pathu8);
	} else {
		d ("(Cannot find namespace for '%s')\n", full_name);
		pathu8 = camel_imapx_store_summary_full_to_path (s, full_name, dir_sep);
	}

	info = (CamelIMAPXStoreInfo *) camel_store_summary_add_from_path ((CamelStoreSummary *) s, pathu8);
	if (info) {
		d ("  '%s' -> '%s'\n", pathu8, full_name);
		camel_store_info_set_string ((CamelStoreSummary *) s, (CamelStoreInfo *) info, CAMEL_IMAPX_STORE_INFO_FULL_NAME, full_name);

		if (!g_ascii_strcasecmp (full_name, "inbox"))
			info->info.flags |= CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX;
	} else {
		d ("  failed\n");
	}

	g_free (pathu8);

	return info;
}

/* should this be const? */
/* TODO: deprecate/merge this function with path_to_full */
gchar *
camel_imapx_store_summary_full_from_path (CamelIMAPXStoreSummary *s,
                                          const gchar *path)
{
	CamelIMAPXStoreNamespace *ns;
	gchar *name = NULL;

	ns = camel_imapx_store_summary_namespace_find_path (s, path);
	if (ns)
		name = camel_imapx_store_summary_path_to_full (s, path, ns->sep);

	d ("looking up path %s -> %s\n", path, name ? name:"not found");

	return name;
}

/* TODO: this api needs some more work */
CamelIMAPXStoreNamespace *
camel_imapx_store_summary_namespace_new (CamelIMAPXStoreSummary *s,
                                         const gchar *full_name,
                                         gchar dir_sep)
{
	CamelIMAPXStoreNamespace *ns;
	gchar *p, *o, c;
	gint len;

	ns = g_malloc0 (sizeof (*ns));
	ns->full_name = g_strdup (full_name);
	len = strlen (ns->full_name) - 1;
	if (len >= 0 && ns->full_name[len] == dir_sep)
		ns->full_name[len] = 0;
	ns->sep = dir_sep;

	o = p = ns->path = camel_imapx_store_summary_full_to_path (s, ns->full_name, dir_sep);
	while ((c = *p++)) {
		if (c != '#') {
			if (c == '/')
				c = '.';
			*o++ = c;
		}
	}
	*o = 0;

	return ns;
}

void camel_imapx_store_summary_namespace_set (CamelIMAPXStoreSummary *s, CamelIMAPXStoreNamespace *ns)
{
	d ("Setting namesapce to '%s' '%c' -> '%s'\n", ns->full_name, ns->sep, ns->path);

	/* CHEN not needed  */
	camel_store_summary_touch ((CamelStoreSummary *) s);
}

CamelIMAPXStoreNamespace *
camel_imapx_store_summary_namespace_find_path (CamelIMAPXStoreSummary *s,
                                               const gchar *path)
{
	gint len;
	CamelIMAPXStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	/* CHEN TODO */
	ns = s->namespaces->personal;
	while (ns) {
		len = strlen (ns->path);
		if (len == 0
		    || (strncmp (ns->path, path, len) == 0
			&& (path[len] == '/' || path[len] == 0)))
			break;
		ns = NULL;
	}

	/* have a default? */
	return ns;
}

CamelIMAPXStoreNamespace *
camel_imapx_store_summary_namespace_find_full (CamelIMAPXStoreSummary *s,
                                               const gchar *full)
{
	gint len = 0;
	CamelIMAPXStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	/* CHEN TODO */
	ns = s->namespaces->personal;
	while (ns) {
		if (ns->full_name)
			len = strlen (ns->full_name);
		d ("find_full: comparing namespace '%s' to name '%s'\n", ns->full_name, full);
		if (len == 0
		    || (strncmp (ns->full_name, full, len) == 0
			&& (full[len] == ns->sep || full[len] == 0)))
			break;
		ns = NULL;
	}

	/* have a default? */
	return ns;
}

static CamelIMAPXNamespaceList *
namespace_load (CamelStoreSummary *s,
                FILE *in)
{
	CamelIMAPXStoreNamespace *ns, *tail;
	CamelIMAPXNamespaceList *nsl;
	guint32 i, j;
	gint32 n;

	nsl = g_malloc0 (sizeof (CamelIMAPXNamespaceList));
	nsl->personal = NULL;
	nsl->shared = NULL;
	nsl->other = NULL;

	for (j = 0; j < 3; j++) {
		switch (j) {
		case 0:
			tail = (CamelIMAPXStoreNamespace *) &nsl->personal;
			break;
		case 1:
			tail = (CamelIMAPXStoreNamespace *) &nsl->shared;
			break;
		case 2:
			tail = (CamelIMAPXStoreNamespace *) &nsl->other;
			break;
		}

		if (camel_file_util_decode_fixed_int32 (in, &n) == -1)
			goto exception;

		for (i = 0; i < n; i++) {
			guint32 sep;
			gchar *path;
			gchar *full_name;

			if (camel_file_util_decode_string (in, &path) == -1)
				goto exception;

			if (camel_file_util_decode_string (in, &full_name) == -1) {
				g_free (path);
				goto exception;
			}

			if (camel_file_util_decode_uint32 (in, &sep) == -1) {
				g_free (path);
				g_free (full_name);
				goto exception;
			}

			tail->next = ns = g_malloc (sizeof (CamelIMAPXStoreNamespace));
			ns->sep = sep;
			ns->path = path;
			ns->full_name = full_name;
			ns->next = NULL;
			tail = ns;
		}
	}

	return nsl;
exception:
	camel_imapx_namespace_list_clear (nsl);

	return NULL;
}

static gint
namespace_save (CamelStoreSummary *s,
                FILE *out,
                CamelIMAPXNamespaceList *nsl)
{
	CamelIMAPXStoreNamespace *ns, *cur = NULL;
	guint32 i, n;

	for (i = 0; i < 3; i++) {
		switch (i) {
		case 0:
			cur = nsl->personal;
			break;
		case 1:
			cur = nsl->shared;
			break;
		case 2:
			cur = nsl->other;
			break;
		}

		for (ns = cur, n = 0; ns; n++)
			ns = ns->next;

		if (camel_file_util_encode_fixed_int32 (out, n) == -1)
			return -1;

		ns = cur;
		while (ns != NULL) {
			if (camel_file_util_encode_string (out, ns->path) == -1)
				return -1;

			if (camel_file_util_encode_string (out, ns->full_name) == -1)
				return -1;

			if (camel_file_util_encode_uint32 (out, ns->sep) == -1)
				return -1;

			ns = ns->next;
		}
	}

	return 0;
}

static gint
summary_header_load (CamelStoreSummary *s,
                     FILE *in)
{
	CamelIMAPXStoreSummary *is = (CamelIMAPXStoreSummary *) s;
	CamelStoreSummaryClass *store_summary_class;
	gint32 version, capabilities;

	camel_imapx_namespace_list_clear (is->namespaces);

	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (camel_imapx_store_summary_parent_class);
	if (store_summary_class->summary_header_load ((CamelStoreSummary *) s, in) == -1
	    || camel_file_util_decode_fixed_int32 (in, &version) == -1)
		return -1;

	is->version = version;

	if (version < CAMEL_IMAPX_STORE_SUMMARY_VERSION_0) {
		g_warning ("Store summary header version too low");
		return -1;
	}

	/* note file format can be expanded to contain more namespaces, but only 1 at the moment */
	if (camel_file_util_decode_fixed_int32 (in, &capabilities) == -1)
		return -1;

	is->capabilities = capabilities;

	/* namespaces */
	if ((is->namespaces = namespace_load (s, in)) == NULL)
		return -1;

	return 0;
}

static gint
summary_header_save (CamelStoreSummary *s,
                     FILE *out)
{
	CamelIMAPXStoreSummary *is = (CamelIMAPXStoreSummary *) s;
	CamelStoreSummaryClass *store_summary_class;

	/* always write as latest version */
	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (camel_imapx_store_summary_parent_class);
	if (store_summary_class->summary_header_save ((CamelStoreSummary *) s, out) == -1
	    || camel_file_util_encode_fixed_int32 (out, CAMEL_IMAPX_STORE_SUMMARY_VERSION) == -1
	    || camel_file_util_encode_fixed_int32 (out, is->capabilities) == -1)
		return -1;

	if (is->namespaces && namespace_save (s, out, is->namespaces) == -1)
		return -1;

	return 0;
}

static CamelStoreInfo *
store_info_load (CamelStoreSummary *s,
                 FILE *in)
{
	CamelIMAPXStoreInfo *mi;
	CamelStoreSummaryClass *store_summary_class;

	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (camel_imapx_store_summary_parent_class);
	mi = (CamelIMAPXStoreInfo *) store_summary_class->store_info_load (s, in);
	if (mi) {
		if (camel_file_util_decode_string (in, &mi->full_name) == -1) {
			camel_store_summary_info_free (s, (CamelStoreInfo *) mi);
			mi = NULL;
		} else {
			/* NB: this is done again for compatability */
			if (g_ascii_strcasecmp (mi->full_name, "inbox") == 0)
				mi->info.flags |= CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX;
		}
	}

	return (CamelStoreInfo *) mi;
}

static gint
store_info_save (CamelStoreSummary *s,
                 FILE *out,
                 CamelStoreInfo *mi)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *) mi;
	CamelStoreSummaryClass *store_summary_class;

	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (camel_imapx_store_summary_parent_class);
	if (store_summary_class->store_info_save (s, out, mi) == -1
	    || camel_file_util_encode_string (out, isi->full_name) == -1)
		return -1;

	return 0;
}

static void
store_info_free (CamelStoreSummary *s,
                 CamelStoreInfo *mi)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *) mi;
	CamelStoreSummaryClass *store_summary_class;

	g_free (isi->full_name);

	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (camel_imapx_store_summary_parent_class);
	store_summary_class->store_info_free (s, mi);
}

static const gchar *
store_info_string (CamelStoreSummary *s,
                   const CamelStoreInfo *mi,
                   gint type)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *) mi;
	CamelStoreSummaryClass *store_summary_class;

	/* FIXME: Locks? */

	g_assert (mi != NULL);

	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (camel_imapx_store_summary_parent_class);

	switch (type) {
	case CAMEL_IMAPX_STORE_INFO_FULL_NAME:
		return isi->full_name;
	default:
		return store_summary_class->store_info_string (s, mi, type);
	}
}

static void
store_info_set_string (CamelStoreSummary *s,
                       CamelStoreInfo *mi,
                       gint type,
                       const gchar *str)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *) mi;
	CamelStoreSummaryClass *store_summary_class;

	g_assert (mi != NULL);

	store_summary_class = CAMEL_STORE_SUMMARY_CLASS (camel_imapx_store_summary_parent_class);

	switch (type) {
	case CAMEL_IMAPX_STORE_INFO_FULL_NAME:
		d ("Set full name %s -> %s\n", isi->full_name, str);
		camel_store_summary_lock (s, CAMEL_STORE_SUMMARY_SUMMARY_LOCK);
		g_free (isi->full_name);
		isi->full_name = g_strdup (str);
		camel_store_summary_unlock (s, CAMEL_STORE_SUMMARY_SUMMARY_LOCK);
		break;
	default:
		store_summary_class->store_info_set_string (s, mi, type, str);
		break;
	}
}

void
camel_imapx_store_summary_set_namespaces (CamelIMAPXStoreSummary *summary,
                                          const CamelIMAPXNamespaceList *nsl)
{
	if (summary->namespaces)
		camel_imapx_namespace_list_clear (summary->namespaces);
	summary->namespaces = camel_imapx_namespace_list_copy (nsl);
}

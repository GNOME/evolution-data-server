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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "camel-nntp-store-summary.h"

#define d(x)
#define io(x)			/* io debug */

#define CAMEL_NNTP_STORE_SUMMARY_VERSION_0 (0)
#define CAMEL_NNTP_STORE_SUMMARY_VERSION_1 (1)

#define CAMEL_NNTP_STORE_SUMMARY_VERSION (1)

static gint summary_header_load (CamelStoreSummary *, FILE *);
static gint summary_header_save (CamelStoreSummary *, FILE *);

static CamelStoreInfo * store_info_load (CamelStoreSummary *, FILE *);
static gint		 store_info_save (CamelStoreSummary *, FILE *, CamelStoreInfo *);
static void		 store_info_free (CamelStoreSummary *, CamelStoreInfo *);

static const gchar *store_info_string (CamelStoreSummary *, const CamelStoreInfo *, gint);
static void store_info_set_string (CamelStoreSummary *, CamelStoreInfo *, int, const gchar *);

G_DEFINE_TYPE (CamelNNTPStoreSummary, camel_nntp_store_summary, CAMEL_TYPE_STORE_SUMMARY)

static void
camel_nntp_store_summary_class_init (CamelNNTPStoreSummaryClass *class)
{
	CamelStoreSummaryClass *store_summary_class;

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
camel_nntp_store_summary_init (CamelNNTPStoreSummary *nntp_store_summary)
{
	CamelStoreSummary *store_summary;

	store_summary = CAMEL_STORE_SUMMARY (nntp_store_summary);
	store_summary->store_info_size = sizeof (CamelNNTPStoreInfo);

	nntp_store_summary->version = CAMEL_NNTP_STORE_SUMMARY_VERSION;

	memset (
		&nntp_store_summary->last_newslist, 0,
		sizeof (nntp_store_summary->last_newslist));
}

/**
 * camel_nntp_store_summary_new:
 *
 * Create a new CamelNNTPStoreSummary object.
 *
 * Returns: A new CamelNNTPStoreSummary widget.
 **/
CamelNNTPStoreSummary *
camel_nntp_store_summary_new (void)
{
	return g_object_new (CAMEL_TYPE_NNTP_STORE_SUMMARY, NULL);
}

/**
 * camel_nntp_store_summary_full_name:
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
CamelNNTPStoreInfo *
camel_nntp_store_summary_full_name (CamelNNTPStoreSummary *s,
                                    const gchar *full_name)
{
	gint count, i;
	CamelNNTPStoreInfo *info;

	count = camel_store_summary_count ((CamelStoreSummary *) s);
	for (i = 0; i < count; i++) {
		info = (CamelNNTPStoreInfo *) camel_store_summary_index ((CamelStoreSummary *) s, i);
		if (info) {
			if (strcmp (info->full_name, full_name) == 0)
				return info;
			camel_store_summary_info_free ((CamelStoreSummary *) s, (CamelStoreInfo *) info);
		}
	}

	return NULL;
}

gchar *
camel_nntp_store_summary_full_to_path (CamelNNTPStoreSummary *s,
                                       const gchar *full_name,
                                       gchar dir_sep)
{
	gchar *path, *p;
	gint c;
	const gchar *f;

	if (dir_sep != '/') {
		p = path = g_alloca (strlen (full_name) * 3 + 1);
		f = full_name;
		while ((c = *f++ & 0xff)) {
			if (c == dir_sep)
				*p++ = '/';
			else if (c == '/' || c == '%')
				p += sprintf (p, "%%%02X", c);
			else
				*p++ = c;
		}
		*p = 0;
	} else
		path = (gchar *) full_name;

	return camel_utf7_utf8 (path);
}

static guint32
hexnib (guint32 c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	else
		return 0;
}

gchar *
camel_nntp_store_summary_path_to_full (CamelNNTPStoreSummary *s,
                                       const gchar *path,
                                       gchar dir_sep)
{
	gchar *full, *f;
	guint32 c, v = 0;
	const gchar *p;
	gint state = 0;
	gchar *subpath, *last = NULL;
	CamelStoreInfo *si;

	/* check to see if we have a subpath of path already defined */
	subpath = g_alloca (strlen (path) + 1);
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
		f = g_strdup (camel_nntp_store_info_full_name (s, si));
		camel_store_summary_info_free ((CamelStoreSummary *) s, si);
		return f;
	}

	f = full = g_alloca (strlen (path) * 2 + 1);
	if (si)
		p = path + strlen (subpath);
	else
		p = path;

	while ((c = camel_utf8_getc ((const guchar **) &p))) {
		switch (state) {
		case 0:
			if (c == '%') {
				state = 1;
			} else {
				if (c == '/')
					c = dir_sep;
				camel_utf8_putc ((guchar **) &f, c);
			}
			break;
		case 1:
			state = 2;
			v = hexnib (c) << 4;
			break;
		case 2:
			state = 0;
			v |= hexnib (c);
			camel_utf8_putc ((guchar **) &f, v);
			break;
		}
	}
	camel_utf8_putc ((guchar **) &f, c);

	/* merge old path part if required */
	f = camel_utf8_utf7 (full);
	if (si) {
		full = g_strdup_printf ("%s%s", camel_nntp_store_info_full_name (s, si), f);
		g_free (f);
		camel_store_summary_info_free ((CamelStoreSummary *) s, si);
		f = full;
	}

	return f;
}

CamelNNTPStoreInfo *
camel_nntp_store_summary_add_from_full (CamelNNTPStoreSummary *s,
                                        const gchar *full,
                                        gchar dir_sep)
{
	CamelNNTPStoreInfo *info;
	gchar *pathu8;
	gint len;
	gchar *full_name;

	d (printf ("adding full name '%s' '%c'\n", full, dir_sep));

	len = strlen (full);
	full_name = g_alloca (len + 1);
	strcpy (full_name, full);
	if (full_name[len - 1] == dir_sep)
		full_name[len - 1] = 0;

	info = camel_nntp_store_summary_full_name (s, full_name);
	if (info) {
		camel_store_summary_info_free ((CamelStoreSummary *) s, (CamelStoreInfo *) info);
		d (printf ("  already there\n"));
		return info;
	}

	pathu8 = camel_nntp_store_summary_full_to_path (s, full_name, dir_sep);

	info = (CamelNNTPStoreInfo *) camel_store_summary_add_from_path ((CamelStoreSummary *) s, pathu8);
	if (info) {
		d (printf ("  '%s' -> '%s'\n", pathu8, full_name));
		camel_store_info_set_string ((CamelStoreSummary *) s, (CamelStoreInfo *) info, CAMEL_NNTP_STORE_INFO_FULL_NAME, full_name);
	} else {
		d (printf ("  failed\n"));
	}

	return info;
}

static gint
summary_header_load (CamelStoreSummary *s,
                     FILE *in)
{
	CamelNNTPStoreSummary *is = (CamelNNTPStoreSummary *) s;
	gint32 version, nil;

	if (CAMEL_STORE_SUMMARY_CLASS (camel_nntp_store_summary_parent_class)->summary_header_load ((CamelStoreSummary *) s, in) == -1
	    || camel_file_util_decode_fixed_int32 (in, &version) == -1)
		return -1;

	is->version = version;

	if (version < CAMEL_NNTP_STORE_SUMMARY_VERSION_0) {
		g_warning ("Store summary header version too low");
		return -1;
	}

	if (fread (is->last_newslist, 1, NNTP_DATE_SIZE, in) < NNTP_DATE_SIZE)
		return -1;

	return camel_file_util_decode_fixed_int32 (in, &nil);
}

static gint
summary_header_save (CamelStoreSummary *s,
                     FILE *out)
{
	CamelNNTPStoreSummary *is = (CamelNNTPStoreSummary *) s;

	/* always write as latest version */
	if (CAMEL_STORE_SUMMARY_CLASS (camel_nntp_store_summary_parent_class)->summary_header_save ((CamelStoreSummary *) s, out) == -1
	    || camel_file_util_encode_fixed_int32 (out, CAMEL_NNTP_STORE_SUMMARY_VERSION) == -1
	    || fwrite (is->last_newslist, 1, NNTP_DATE_SIZE, out) < NNTP_DATE_SIZE
	    || camel_file_util_encode_fixed_int32 (out, 0) == -1)
		return -1;

	return 0;
}

static CamelStoreInfo *
store_info_load (CamelStoreSummary *s,
                 FILE *in)
{
	CamelNNTPStoreInfo *ni;

	ni = (CamelNNTPStoreInfo *) CAMEL_STORE_SUMMARY_CLASS (camel_nntp_store_summary_parent_class)->store_info_load (s, in);
	if (ni) {
		if (camel_file_util_decode_string (in, &ni->full_name) == -1) {
			camel_store_summary_info_free (s, (CamelStoreInfo *) ni);
			return NULL;
		}
		if (((CamelNNTPStoreSummary *) s)->version >= CAMEL_NNTP_STORE_SUMMARY_VERSION_1) {
			if (camel_file_util_decode_uint32 (in, &ni->first) == -1
			    || camel_file_util_decode_uint32 (in, &ni->last) == -1) {
				camel_store_summary_info_free (s, (CamelStoreInfo *) ni);
				return NULL;
			}
		}
		/* set the URL */
	}

	return (CamelStoreInfo *) ni;
}

static gint
store_info_save (CamelStoreSummary *s,
                 FILE *out,
                 CamelStoreInfo *mi)
{
	CamelNNTPStoreInfo *isi = (CamelNNTPStoreInfo *) mi;

	if (CAMEL_STORE_SUMMARY_CLASS (camel_nntp_store_summary_parent_class)->store_info_save (s, out, mi) == -1
	    || camel_file_util_encode_string (out, isi->full_name) == -1
	    || camel_file_util_encode_uint32 (out, isi->first) == -1
	    || camel_file_util_encode_uint32 (out, isi->last) == -1)
		return -1;

	return 0;
}

static void
store_info_free (CamelStoreSummary *s,
                 CamelStoreInfo *mi)
{
	CamelNNTPStoreInfo *nsi = (CamelNNTPStoreInfo *) mi;

	g_free (nsi->full_name);
	CAMEL_STORE_SUMMARY_CLASS (camel_nntp_store_summary_parent_class)->store_info_free (s, mi);
}

static const gchar *
store_info_string (CamelStoreSummary *s,
                   const CamelStoreInfo *mi,
                   gint type)
{
	CamelNNTPStoreInfo *nsi = (CamelNNTPStoreInfo *) mi;

	/* FIXME: Locks? */

	g_assert (mi != NULL);

	switch (type) {
	case CAMEL_NNTP_STORE_INFO_FULL_NAME:
		return nsi->full_name;
	default:
		return CAMEL_STORE_SUMMARY_CLASS (camel_nntp_store_summary_parent_class)->store_info_string (s, mi, type);
	}
}

static void
store_info_set_string (CamelStoreSummary *s,
                       CamelStoreInfo *mi,
                       gint type,
                       const gchar *str)
{
	CamelNNTPStoreInfo *nsi = (CamelNNTPStoreInfo *) mi;

	g_assert (mi != NULL);

	switch (type) {
	case CAMEL_NNTP_STORE_INFO_FULL_NAME:
		d (printf ("Set full name %s -> %s\n", nsi->full_name, str));
		camel_store_summary_lock (s, CAMEL_STORE_SUMMARY_SUMMARY_LOCK);
		g_free (nsi->full_name);
		nsi->full_name = g_strdup (str);
		camel_store_summary_unlock (s, CAMEL_STORE_SUMMARY_SUMMARY_LOCK);
		break;
	default:
		CAMEL_STORE_SUMMARY_CLASS (camel_nntp_store_summary_parent_class)->store_info_set_string (s, mi, type, str);
		break;
	}
}

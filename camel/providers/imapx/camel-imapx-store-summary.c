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

#include "camel-file-utils.h"
#include "camel-private.h"
#include "camel-store.h"
#include "camel-utf8.h"
#include "camel-imapx-utils.h"
#include "camel-imapx-store-summary.h"

#define d(x)
#define io(x)			/* io debug */

#define CAMEL_IMAPX_STORE_SUMMARY_VERSION_0 (0)

#define CAMEL_IMAPX_STORE_SUMMARY_VERSION (0)

#define _PRIVATE(o) (((CamelIMAPXStoreSummary *)(o))->priv)

static gint summary_header_load(CamelStoreSummary *, FILE *);
static gint summary_header_save(CamelStoreSummary *, FILE *);

/*static CamelStoreInfo * store_info_new(CamelStoreSummary *, const gchar *);*/
static CamelStoreInfo * store_info_load(CamelStoreSummary *, FILE *);
static gint		 store_info_save(CamelStoreSummary *, FILE *, CamelStoreInfo *);
static void		 store_info_free(CamelStoreSummary *, CamelStoreInfo *);

static const gchar *store_info_string(CamelStoreSummary *, const CamelStoreInfo *, gint);
static void store_info_set_string(CamelStoreSummary *, CamelStoreInfo *, int, const gchar *);

static void camel_imapx_store_summary_class_init (CamelIMAPXStoreSummaryClass *klass);
static void camel_imapx_store_summary_init       (CamelIMAPXStoreSummary *obj);
static void camel_imapx_store_summary_finalise   (CamelObject *obj);

static CamelStoreSummaryClass *camel_imapx_store_summary_parent;

static void
camel_imapx_store_summary_class_init (CamelIMAPXStoreSummaryClass *klass)
{
	CamelStoreSummaryClass *ssklass = (CamelStoreSummaryClass *)klass;

	ssklass->summary_header_load = summary_header_load;
	ssklass->summary_header_save = summary_header_save;

	/*ssklass->store_info_new  = store_info_new;*/
	ssklass->store_info_load = store_info_load;
	ssklass->store_info_save = store_info_save;
	ssklass->store_info_free = store_info_free;

	ssklass->store_info_string = store_info_string;
	ssklass->store_info_set_string = store_info_set_string;
}

static void
camel_imapx_store_summary_init (CamelIMAPXStoreSummary *s)
{
	/*struct _CamelImapStoreSummaryPrivate *p;

	  p = _PRIVATE(s) = g_malloc0(sizeof(*p));*/

	((CamelStoreSummary *)s)->store_info_size = sizeof(CamelIMAPXStoreInfo);
	s->version = CAMEL_IMAPX_STORE_SUMMARY_VERSION;
}

static void
camel_imapx_store_summary_finalise (CamelObject *obj)
{
	/*struct _CamelImapStoreSummaryPrivate *p;*/
	/*CamelImapStoreSummary *s = (CamelImapStoreSummary *)obj;*/

	/*p = _PRIVATE(obj);
	  g_free(p);*/
}

CamelType
camel_imapx_store_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_imapx_store_summary_parent = (CamelStoreSummaryClass *)camel_store_summary_get_type();
		type = camel_type_register((CamelType)camel_imapx_store_summary_parent, "CamelIMAPXStoreSummary",
					   sizeof (CamelIMAPXStoreSummary),
					   sizeof (CamelIMAPXStoreSummaryClass),
					   (CamelObjectClassInitFunc) camel_imapx_store_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_imapx_store_summary_init,
					   (CamelObjectFinalizeFunc) camel_imapx_store_summary_finalise);
	}

	return type;
}

/**
 * camel_imapx_store_summary_new:
 *
 * Create a new CamelIMAPXStoreSummary object.
 *
 * Return value: A new CamelIMAPXStoreSummary widget.
 **/
CamelIMAPXStoreSummary *
camel_imapx_store_summary_new (void)
{
	CamelIMAPXStoreSummary *new = CAMEL_IMAPX_STORE_SUMMARY ( camel_object_new (camel_imapx_store_summary_get_type ()));

	return new;
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
 * Return value: The summary item, or NULL if the @full_name name
 * is not available.
 * It must be freed using camel_store_summary_info_free().
 **/
CamelIMAPXStoreInfo *
camel_imapx_store_summary_full_name(CamelIMAPXStoreSummary *s, const gchar *full_name)
{
	gint count, i;
	CamelIMAPXStoreInfo *info;

	count = camel_store_summary_count((CamelStoreSummary *)s);
	for (i=0;i<count;i++) {
		info = (CamelIMAPXStoreInfo *)camel_store_summary_index((CamelStoreSummary *)s, i);
		if (info) {
			if (strcmp(info->full_name, full_name) == 0)
				return info;
			camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		}
	}

	return NULL;
}

gchar *
camel_imapx_store_summary_full_to_path(CamelIMAPXStoreSummary *s, const gchar *full_name, gchar dir_sep)
{
	gchar *path, *p;
	gint c;
	const gchar *f;

	if (dir_sep != '/') {
		p = path = alloca(strlen(full_name)*3+1);
		f = full_name;
		while ((c = *f++ & 0xff)) {
			if (c == dir_sep)
				*p++ = '/';
			else if (c == '/' || c == '%')
				p += sprintf(p, "%%%02X", c);
			else
				*p++ = c;
		}
		*p = 0;
	} else
		path = (gchar *)full_name;

	return g_strdup(path);
}

static guint32 hexnib(guint32 c)
{
	if (c >= '0' && c <= '9')
		return c-'0';
	else if (c>='A' && c <= 'Z')
		return c-'A'+10;
	else
		return 0;
}

gchar *
camel_imapx_store_summary_path_to_full(CamelIMAPXStoreSummary *s, const gchar *path, gchar dir_sep)
{
	gchar *full, *f;
	guint32 c, v = 0;
	const gchar *p;
	gint state=0;
	gchar *subpath, *last = NULL;
	CamelStoreInfo *si;
	CamelIMAPXStoreNamespace *ns;

	/* check to see if we have a subpath of path already defined */
	subpath = alloca(strlen(path)+1);
	strcpy(subpath, path);
	do {
		si = camel_store_summary_path((CamelStoreSummary *)s, subpath);
		if (si == NULL) {
			last = strrchr(subpath, '/');
			if (last)
				*last = 0;
		}
	} while (si == NULL && last);

	/* path is already present, use the raw version we have */
	if (si && strlen(subpath) == strlen(path)) {
		f = g_strdup(camel_imapx_store_info_full_name(s, si));
		camel_store_summary_info_free((CamelStoreSummary *)s, si);
		return f;
	}

	ns = camel_imapx_store_summary_namespace_find_path(s, path);

	f = full = alloca(strlen(path)*2+1);
	if (si)
		p = path + strlen(subpath);
	else if (ns)
		p = path + strlen(ns->path);
	else
		p = path;

	while ((c = camel_utf8_getc((const guchar **)&p))) {
		switch (state) {
		case 0:
			if (c == '%')
				state = 1;
			else {
				if (c == '/')
					c = dir_sep;
				camel_utf8_putc((guchar **) &f, c);
			}
			break;
		case 1:
			state = 2;
			v = hexnib(c)<<4;
			break;
		case 2:
			state = 0;
			v |= hexnib(c);
			camel_utf8_putc((guchar **) &f, v);
			break;
		}
	}
	camel_utf8_putc((guchar **) &f, c);

	/* merge old path part if required */
	f = g_strdup(full);
	if (si) {
		full = g_strdup_printf("%s%s", camel_imapx_store_info_full_name(s, si), f);
		g_free(f);
		camel_store_summary_info_free((CamelStoreSummary *)s, si);
		f = full;
	} else if (ns) {
		full = g_strdup_printf("%s%s", ns->full_name, f);
		g_free(f);
		f = full;
	}

	return f;
}

CamelIMAPXStoreInfo *
camel_imapx_store_summary_add_from_full(CamelIMAPXStoreSummary *s, const gchar *full, gchar dir_sep)
{
	CamelIMAPXStoreInfo *info;
	gchar *pathu8, *prefix;
	gint len;
	gchar *full_name;
	CamelIMAPXStoreNamespace *ns;

	d(printf("adding full name '%s' '%c'\n", full, dir_sep));

	len = strlen(full);
	full_name = alloca(len+1);
	strcpy(full_name, full);
	if (full_name[len-1] == dir_sep)
		full_name[len-1] = 0;

	info = camel_imapx_store_summary_full_name(s, full_name);
	if (info) {
		camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		d(printf("  already there\n"));
		return info;
	}

	ns = camel_imapx_store_summary_namespace_find_full(s, full_name);
	if (ns) {
		d(printf("(found namespace for '%s' ns '%s') ", full_name, ns->path));
		len = strlen(ns->full_name);
		if (len >= strlen(full_name)) {
			pathu8 = g_strdup(ns->path);
		} else {
			if (full_name[len] == ns->sep)
				len++;

			prefix = camel_imapx_store_summary_full_to_path(s, full_name+len, ns->sep);
			if (*ns->path) {
				pathu8 = g_strdup_printf ("%s/%s", ns->path, prefix);
				g_free (prefix);
			} else {
				pathu8 = prefix;
			}
		}
		d(printf(" (pathu8 = '%s')", pathu8));
	} else {
		d(printf("(Cannot find namespace for '%s')\n", full_name));
		pathu8 = camel_imapx_store_summary_full_to_path(s, full_name, dir_sep);
	}

	info = (CamelIMAPXStoreInfo *)camel_store_summary_add_from_path((CamelStoreSummary *)s, pathu8);
	if (info) {
		d(printf("  '%s' -> '%s'\n", pathu8, full_name));
		camel_store_info_set_string((CamelStoreSummary *)s, (CamelStoreInfo *)info, CAMEL_IMAP_STORE_INFO_FULL_NAME, full_name);

		if (!g_ascii_strcasecmp(full_name, "inbox"))
			info->info.flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_TYPE_INBOX;
	} else {
		d(printf("  failed\n"));
	}

	return info;
}

/* should this be const? */
/* TODO: deprecate/merge this function with path_to_full */
gchar *
camel_imapx_store_summary_full_from_path(CamelIMAPXStoreSummary *s, const gchar *path)
{
	CamelIMAPXStoreNamespace *ns;
	gchar *name = NULL;

	ns = camel_imapx_store_summary_namespace_find_path(s, path);
	if (ns)
		name = camel_imapx_store_summary_path_to_full(s, path, ns->sep);

	d(printf("looking up path %s -> %s\n", path, name?name:"not found"));

	return name;
}

/* TODO: this api needs some more work */
CamelIMAPXStoreNamespace *camel_imapx_store_summary_namespace_new(CamelIMAPXStoreSummary *s, const gchar *full_name, gchar dir_sep)
{
	CamelIMAPXStoreNamespace *ns;
	gchar *p, *o, c;
	gint len;

	ns = g_malloc0(sizeof(*ns));
	ns->full_name = g_strdup(full_name);
	len = strlen(ns->full_name)-1;
	if (len >= 0 && ns->full_name[len] == dir_sep)
		ns->full_name[len] = 0;
	ns->sep = dir_sep;

	o = p = ns->path = camel_imapx_store_summary_full_to_path(s, ns->full_name, dir_sep);
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

void camel_imapx_store_summary_namespace_set(CamelIMAPXStoreSummary *s, CamelIMAPXStoreNamespace *ns)
{
	d(printf("Setting namesapce to '%s' '%c' -> '%s'\n", ns->full_name, ns->sep, ns->path));

	/* CHEN not needed  */
	camel_store_summary_touch((CamelStoreSummary *)s);
}

CamelIMAPXStoreNamespace *
camel_imapx_store_summary_namespace_find_path(CamelIMAPXStoreSummary *s, const gchar *path)
{
	gint len;
	CamelIMAPXStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	/* CHEN TODO */
	ns = s->namespaces->personal;
	while (ns) {
		len = strlen(ns->path);
		if (len == 0
		    || (strncmp(ns->path, path, len) == 0
			&& (path[len] == '/' || path[len] == 0)))
			break;
		ns = NULL;
	}

	/* have a default? */
	return ns;
}

CamelIMAPXStoreNamespace *
camel_imapx_store_summary_namespace_find_full(CamelIMAPXStoreSummary *s, const gchar *full)
{
	gint len = 0;
	CamelIMAPXStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	/* CHEN TODO */
	ns = s->namespaces->personal;
	while (ns) {
		if (ns->full_name)
			len = strlen(ns->full_name);
		d(printf("find_full: comparing namespace '%s' to name '%s'\n", ns->full_name, full));
		if (len == 0
		    || (strncmp(ns->full_name, full, len) == 0
			&& (full[len] == ns->sep || full[len] == 0)))
			break;
		ns = NULL;
	}

	/* have a default? */
	return ns;
}

static CamelIMAPXNamespaceList *
namespace_load(CamelStoreSummary *s, FILE *in)
{
	CamelIMAPXStoreNamespace *ns, *tail;
	CamelIMAPXNamespaceList *nsl;
	guint32 i, j;
	gint32 n;

	nsl = g_malloc0(sizeof(CamelIMAPXNamespaceList));
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
namespace_save(CamelStoreSummary *s, FILE *out, CamelIMAPXNamespaceList *nsl)
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
summary_header_load(CamelStoreSummary *s, FILE *in)
{
	CamelIMAPXStoreSummary *is = (CamelIMAPXStoreSummary *)s;
	gint32 version, capabilities;

	camel_imapx_namespace_list_clear (is->namespaces);

	if (camel_imapx_store_summary_parent->summary_header_load((CamelStoreSummary *)s, in) == -1
	    || camel_file_util_decode_fixed_int32(in, &version) == -1)
		return -1;

	is->version = version;

	if (version < CAMEL_IMAPX_STORE_SUMMARY_VERSION_0) {
		g_warning("Store summary header version too low");
		return -1;
	}

	/* note file format can be expanded to contain more namespaces, but only 1 at the moment */
	if (camel_file_util_decode_fixed_int32(in, &capabilities) == -1)
		return -1;

	is->capabilities = capabilities;

	/* namespaces */
	if ((is->namespaces = namespace_load(s, in)) == NULL)
		return -1;

	return 0;
}

static gint
summary_header_save(CamelStoreSummary *s, FILE *out)
{
	CamelIMAPXStoreSummary *is = (CamelIMAPXStoreSummary *)s;

	/* always write as latest version */
	if (camel_imapx_store_summary_parent->summary_header_save((CamelStoreSummary *)s, out) == -1
	    || camel_file_util_encode_fixed_int32(out, CAMEL_IMAPX_STORE_SUMMARY_VERSION) == -1
	    || camel_file_util_encode_fixed_int32(out, is->capabilities) == -1)
		return -1;

	if (is->namespaces && namespace_save(s, out, is->namespaces) == -1)
		return -1;

	return 0;
}

static CamelStoreInfo *
store_info_load(CamelStoreSummary *s, FILE *in)
{
	CamelIMAPXStoreInfo *mi;

	mi = (CamelIMAPXStoreInfo *)camel_imapx_store_summary_parent->store_info_load(s, in);
	if (mi) {
		if (camel_file_util_decode_string(in, &mi->full_name) == -1) {
			camel_store_summary_info_free(s, (CamelStoreInfo *)mi);
			mi = NULL;
		} else {
			/* NB: this is done again for compatability */
			if (g_ascii_strcasecmp(mi->full_name, "inbox") == 0)
				mi->info.flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_TYPE_INBOX;
		}
	}

	return (CamelStoreInfo *)mi;
}

static gint
store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *mi)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *)mi;

	if (camel_imapx_store_summary_parent->store_info_save(s, out, mi) == -1
	    || camel_file_util_encode_string(out, isi->full_name) == -1)
		return -1;

	return 0;
}

static void
store_info_free(CamelStoreSummary *s, CamelStoreInfo *mi)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *)mi;

	g_free(isi->full_name);
	camel_imapx_store_summary_parent->store_info_free(s, mi);
}

static const gchar *
store_info_string(CamelStoreSummary *s, const CamelStoreInfo *mi, gint type)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *)mi;

	/* FIXME: Locks? */

	g_assert (mi != NULL);

	switch (type) {
	case CAMEL_IMAP_STORE_INFO_FULL_NAME:
		return isi->full_name;
	default:
		return camel_imapx_store_summary_parent->store_info_string(s, mi, type);
	}
}

static void
store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *mi, gint type, const gchar *str)
{
	CamelIMAPXStoreInfo *isi = (CamelIMAPXStoreInfo *)mi;

	g_assert(mi != NULL);

	switch (type) {
	case CAMEL_IMAP_STORE_INFO_FULL_NAME:
		d(printf("Set full name %s -> %s\n", isi->full_name, str));
		CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
		g_free(isi->full_name);
		isi->full_name = g_strdup(str);
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		break;
	default:
		camel_imapx_store_summary_parent->store_info_set_string(s, mi, type, str);
		break;
	}
}

void
camel_imapx_store_summary_set_namespaces (CamelIMAPXStoreSummary *summary, const CamelIMAPXNamespaceList *nsl)
{
	if (summary->namespaces)
		camel_imapx_namespace_list_clear (summary->namespaces);
	summary->namespaces = camel_imapx_namespace_list_copy (summary->namespaces);
}

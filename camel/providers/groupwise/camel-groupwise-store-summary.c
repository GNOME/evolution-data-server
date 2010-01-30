/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Parthasarathi Susarla <sparthasarathi@novell.com>
 *
 * Description: Based on the imap implementaion of camelstoresummary
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
#include "camel-utf8.h"

#include "camel-groupwise-store-summary.h"

#define CAMEL_GW_STORE_SUMMARY_VERSION (0)

#define d(x)

static void namespace_clear(CamelStoreSummary *s);

static gint summary_header_load(CamelStoreSummary *, FILE *);
static gint summary_header_save(CamelStoreSummary *, FILE *);

static CamelStoreInfo *store_info_load(CamelStoreSummary *s, FILE *in);
static gint store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *mi);
static void store_info_free(CamelStoreSummary *s, CamelStoreInfo *mi);
static void store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *mi, gint type, const gchar *str);

static const gchar *store_info_string(CamelStoreSummary *s, const CamelStoreInfo *mi, gint type);
CamelGroupwiseStoreNamespace *camel_groupwise_store_summary_namespace_find_full(CamelGroupwiseStoreSummary *s, const gchar *full);

static void camel_groupwise_store_summary_class_init (CamelGroupwiseStoreSummaryClass *klass);
static void camel_groupwise_store_summary_init       (CamelGroupwiseStoreSummary *obj);
static void camel_groupwise_store_summary_finalise   (CamelObject *obj);

static CamelStoreSummaryClass *camel_groupwise_store_summary_parent;

static void
camel_groupwise_store_summary_class_init (CamelGroupwiseStoreSummaryClass *klass)
{
	CamelStoreSummaryClass *ssklass = (CamelStoreSummaryClass *)klass;

	ssklass->summary_header_load = summary_header_load;
	ssklass->summary_header_save = summary_header_save;

	ssklass->store_info_load = store_info_load;
	ssklass->store_info_save = store_info_save;
	ssklass->store_info_free = store_info_free;

	ssklass->store_info_string = store_info_string;
	ssklass->store_info_set_string = store_info_set_string;

}

static void
camel_groupwise_store_summary_init (CamelGroupwiseStoreSummary *s)
{

	((CamelStoreSummary *)s)->store_info_size = sizeof(CamelGroupwiseStoreInfo);
	s->version = CAMEL_GW_STORE_SUMMARY_VERSION;
}

static void
camel_groupwise_store_summary_finalise (CamelObject *obj)
{
}

CamelType
camel_groupwise_store_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_groupwise_store_summary_parent = (CamelStoreSummaryClass *)camel_store_summary_get_type();
		type = camel_type_register((CamelType)camel_groupwise_store_summary_parent, "CamelGroupwiseStoreSummary",
				sizeof (CamelGroupwiseStoreSummary),
				sizeof (CamelGroupwiseStoreSummaryClass),
				(CamelObjectClassInitFunc) camel_groupwise_store_summary_class_init,
				NULL,
				(CamelObjectInitFunc) camel_groupwise_store_summary_init,
				(CamelObjectFinalizeFunc) camel_groupwise_store_summary_finalise);
	}

	return type;
}

CamelGroupwiseStoreSummary *
camel_groupwise_store_summary_new (void)
{
	CamelGroupwiseStoreSummary *new = CAMEL_GW_STORE_SUMMARY ( camel_object_new (camel_groupwise_store_summary_get_type ()));

	return new;
}

CamelGroupwiseStoreInfo *
camel_groupwise_store_summary_full_name(CamelGroupwiseStoreSummary *s, const gchar *full_name)
{
	gint count, i;
	CamelGroupwiseStoreInfo *info;

	count = camel_store_summary_count((CamelStoreSummary *)s);
	for (i=0;i<count;i++) {
		info = (CamelGroupwiseStoreInfo *)camel_store_summary_index((CamelStoreSummary *)s, i);
		if (info) {
			if (strcmp(info->full_name, full_name) == 0)
				return info;
			camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		}
	}

	return NULL;
}

gchar *
camel_groupwise_store_summary_full_to_path(CamelGroupwiseStoreSummary *s, const gchar *full_name, gchar dir_sep)
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

	return g_strdup (path);
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

static gint
namespace_save(CamelStoreSummary *s, FILE *in, CamelGroupwiseStoreNamespace *ns)
{
	if (camel_file_util_encode_string(in, ns->path) == -1
			|| camel_file_util_encode_string(in, ns->full_name) == -1
			|| camel_file_util_encode_uint32(in, (guint32)ns->sep) == -1)
		return -1;

	return 0;
}

static void
namespace_free(CamelStoreSummary *s, CamelGroupwiseStoreNamespace *ns)
{
	g_free(ns->path);
	g_free(ns->full_name);
	g_free(ns);
}

static void
namespace_clear(CamelStoreSummary *s)
{
	CamelGroupwiseStoreSummary *is = (CamelGroupwiseStoreSummary *)s;

	if (is->namespace)
		namespace_free(s, is->namespace);
	is->namespace = NULL;
}

static CamelGroupwiseStoreNamespace *
namespace_load(CamelStoreSummary *s, FILE *in)
{
	CamelGroupwiseStoreNamespace *ns;
	guint32 sep = '/';

	ns = g_malloc0(sizeof(*ns));
	if (camel_file_util_decode_string(in, &ns->path) == -1
			|| camel_file_util_decode_string(in, &ns->full_name) == -1
			|| camel_file_util_decode_uint32(in, &sep) == -1) {
		namespace_free(s, ns);
		ns = NULL;
	} else {
		ns->sep = sep;
	}

	return ns;
}

gchar *
camel_groupwise_store_summary_path_to_full(CamelGroupwiseStoreSummary *s, const gchar *path, gchar dir_sep)
{
	gchar *full, *f;
	guint32 c, v = 0;
	const gchar *p;
	gint state=0;
	gchar *subpath, *last = NULL;
	CamelStoreInfo *si;
	CamelGroupwiseStoreNamespace *ns;

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
		f = g_strdup(camel_groupwise_store_info_full_name(s, si));
		camel_store_summary_info_free((CamelStoreSummary *)s, si);
		return f;
	}

	ns = camel_groupwise_store_summary_namespace_find_path(s, path);

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
	f = g_strdup (full);
	if (si) {
		full = g_strdup_printf("%s%s", camel_groupwise_store_info_full_name(s, si), f);
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

CamelGroupwiseStoreNamespace *
camel_groupwise_store_summary_namespace_find_full(CamelGroupwiseStoreSummary *s, const gchar *full)
{
	gint len;
	CamelGroupwiseStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	ns = s->namespace;
	while (ns) {
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

CamelGroupwiseStoreInfo *
camel_groupwise_store_summary_add_from_full(CamelGroupwiseStoreSummary *s, const gchar *full, gchar dir_sep)
{
	CamelGroupwiseStoreInfo *info;
	gchar *pathu8, *prefix;
	gint len;
	gchar *full_name;
	CamelGroupwiseStoreNamespace *ns;

	d(printf("adding full name '%s' '%c'\n", full, dir_sep));

	len = strlen(full);
	full_name = alloca(len+1);
	strcpy(full_name, full);
	if (full_name[len-1] == dir_sep)
		full_name[len-1] = 0;

	info = camel_groupwise_store_summary_full_name(s, full_name);
	if (info) {
		camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		d(printf("  already there\n"));
		return info;
	}

	ns = camel_groupwise_store_summary_namespace_find_full(s, full_name);
	if (ns) {
		d(printf("(found namespace for '%s' ns '%s') ", full_name, ns->path));
		len = strlen(ns->full_name);
		if (len >= strlen(full_name)) {
			pathu8 = g_strdup(ns->path);
		} else {
			if (full_name[len] == ns->sep)
				len++;

			prefix = camel_groupwise_store_summary_full_to_path(s, full_name+len, ns->sep);
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
		pathu8 = camel_groupwise_store_summary_full_to_path(s, full_name, dir_sep);
	}

	info = (CamelGroupwiseStoreInfo *)camel_store_summary_add_from_path((CamelStoreSummary *)s, pathu8);
	if (info) {
		d(printf("  '%s' -> '%s'\n", pathu8, full_name));
		camel_store_info_set_string((CamelStoreSummary *)s, (CamelStoreInfo *)info, CAMEL_STORE_INFO_LAST, full_name);
	} else {
		d(printf("  failed\n"));
	}

	return info;
}

gchar *
camel_groupwise_store_summary_full_from_path(CamelGroupwiseStoreSummary *s, const gchar *path)
{
	CamelGroupwiseStoreNamespace *ns;
	gchar *name = NULL;

	ns = camel_groupwise_store_summary_namespace_find_path(s, path);
	if (ns)
		name = camel_groupwise_store_summary_path_to_full(s, path, ns->sep);

	d(printf("looking up path %s -> %s\n", path, name?name:"not found"));

	return name;
}

CamelGroupwiseStoreNamespace *
camel_groupwise_store_summary_namespace_new(CamelGroupwiseStoreSummary *s, const gchar *full_name, gchar dir_sep)
{
	CamelGroupwiseStoreNamespace *ns;
	gchar *p, *o, c;
	gint len;

	ns = g_malloc0(sizeof(*ns));
	ns->full_name = g_strdup(full_name);
	len = strlen(ns->full_name)-1;
	if (len >= 0 && ns->full_name[len] == dir_sep)
		ns->full_name[len] = 0;
	ns->sep = dir_sep;

	o = p = ns->path = camel_groupwise_store_summary_full_to_path(s, ns->full_name, dir_sep);
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

void
camel_groupwise_store_summary_namespace_set(CamelGroupwiseStoreSummary *s, CamelGroupwiseStoreNamespace *ns)
{
	d(printf("Setting namesapce to '%s' '%c' -> '%s'\n", ns->full_name, ns->sep, ns->path));
	namespace_clear((CamelStoreSummary *)s);
	s->namespace = ns;
	camel_store_summary_touch((CamelStoreSummary *)s);
}

CamelGroupwiseStoreNamespace *
camel_groupwise_store_summary_namespace_find_path(CamelGroupwiseStoreSummary *s, const gchar *path)
{
	gint len;
	CamelGroupwiseStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	ns = s->namespace;
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

static gint
summary_header_load(CamelStoreSummary *s, FILE *in)
{
	CamelGroupwiseStoreSummary *summary = (CamelGroupwiseStoreSummary *)s;
	 gint32 version, capabilities, count;

	namespace_clear (s);

	if (camel_groupwise_store_summary_parent->summary_header_load ((CamelStoreSummary *)s, in) == -1
			|| camel_file_util_decode_fixed_int32(in, &version) == -1)
		return -1;

	summary->version = version;

	if (camel_file_util_decode_fixed_int32(in, &capabilities) == -1
			|| camel_file_util_decode_fixed_int32(in, &count) == -1
			|| count > 1)
		return -1;

	summary->capabilities = capabilities;
	if (count == 1) {
		if ((summary->namespace = namespace_load (s, in)) == NULL)
			return -1;
	}
	return 0;
}

static gint
summary_header_save(CamelStoreSummary *s, FILE *out)
{
	CamelGroupwiseStoreSummary *summary = (CamelGroupwiseStoreSummary *) s;
	guint32 count;

	count = summary->namespace?1:0;
	if (camel_groupwise_store_summary_parent->summary_header_save((CamelStoreSummary *)s, out) == -1
			|| camel_file_util_encode_fixed_int32(out, 0) == -1
			|| camel_file_util_encode_fixed_int32(out, summary->capabilities) == -1
			|| camel_file_util_encode_fixed_int32(out, count) == -1)
		return -1;

	if (summary->namespace && namespace_save(s, out, summary->namespace) == -1)
		return -1;

	return 0;
}

static CamelStoreInfo *
store_info_load(CamelStoreSummary *s, FILE *in)
{
	CamelGroupwiseStoreInfo *si;

	si = (CamelGroupwiseStoreInfo *)camel_groupwise_store_summary_parent->store_info_load(s, in);
	if (si) {
		if (camel_file_util_decode_string(in, &si->full_name) == -1) {
			camel_store_summary_info_free(s, (CamelStoreInfo *)si);
			si = NULL;
		}
	}

	return (CamelStoreInfo *)si;
}

static gint
store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *mi)
{
	CamelGroupwiseStoreInfo *summary = (CamelGroupwiseStoreInfo *)mi;

	if (camel_groupwise_store_summary_parent->store_info_save(s, out, mi) == -1
			|| camel_file_util_encode_string(out, summary->full_name) == -1)
		return -1;

	return 0;
}

static void
store_info_free(CamelStoreSummary *s, CamelStoreInfo *mi)
{
	CamelGroupwiseStoreInfo *si = (CamelGroupwiseStoreInfo *)mi;

	g_free(si->full_name);
	camel_groupwise_store_summary_parent->store_info_free(s, mi);
}

static const gchar *
store_info_string(CamelStoreSummary *s, const CamelStoreInfo *mi, gint type)
{
	CamelGroupwiseStoreInfo *isi = (CamelGroupwiseStoreInfo *)mi;

	/* FIXME: Locks? */

	g_assert (mi != NULL);

	switch (type) {
		case CAMEL_STORE_INFO_LAST:
			return isi->full_name;
		default:
			return camel_groupwise_store_summary_parent->store_info_string(s, mi, type);
	}
}

static void
store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *mi, gint type, const gchar *str)
{
	CamelGroupwiseStoreInfo *isi = (CamelGroupwiseStoreInfo *)mi;

	g_assert(mi != NULL);

	switch (type) {
		case CAMEL_STORE_INFO_LAST:
			d(printf("Set full name %s -> %s\n", isi->full_name, str));
			CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
			g_free(isi->full_name);
			isi->full_name = g_strdup(str);
			CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
			break;
		default:
			camel_groupwise_store_summary_parent->store_info_set_string(s, mi, type, str);
			break;
	}
}


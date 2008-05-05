/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libedataserver/e-memory.h>
#include <libedataserver/md5-utils.h>

#include "camel-file-utils.h"
#include "camel-private.h"
#include "camel-utf8.h"

#include "camel-mapi-store.h"
#include "camel-mapi-store-summary.h"
#include <glib.h>
#include <camel/camel-utf8.h>

#define d(x) 

static int summary_header_load(CamelStoreSummary *, FILE *);
static int summary_header_save(CamelStoreSummary *, FILE *);

static CamelStoreInfo *store_info_load(CamelStoreSummary *s, FILE *in) ;
static int store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *mi) ;
static void store_info_free(CamelStoreSummary *s, CamelStoreInfo *mi) ;
static void store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *mi, int type, const char *str) ;

static const char *store_info_string(CamelStoreSummary *s, const CamelStoreInfo *mi, int type) ;

static void camel_mapi_store_summary_class_init (CamelMapiStoreSummaryClass *klass);
static void camel_mapi_store_summary_init       (CamelMapiStoreSummary *obj);
static void camel_mapi_store_summary_finalise   (CamelObject *obj);

static CamelStoreSummaryClass *camel_mapi_store_summary_parent;


static void
camel_mapi_store_summary_class_init (CamelMapiStoreSummaryClass *klass)
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
camel_mapi_store_summary_init (CamelMapiStoreSummary *s)
{

	((CamelStoreSummary *)s)->store_info_size = sizeof(CamelMapiStoreInfo);
	s->version = CAMEL_MAPI_STORE_SUMMARY_VERSION;
}


static void
camel_mapi_store_summary_finalise (CamelObject *obj)
{
}


CamelType
camel_mapi_store_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_mapi_store_summary_parent = (CamelStoreSummaryClass *)camel_store_summary_get_type();
		type = camel_type_register((CamelType)camel_mapi_store_summary_parent, "CamelMapiStoreSummary",
				sizeof (CamelMapiStoreSummary),
				sizeof (CamelMapiStoreSummaryClass),
				(CamelObjectClassInitFunc) camel_mapi_store_summary_class_init,
				NULL,
				(CamelObjectInitFunc) camel_mapi_store_summary_init,
				(CamelObjectFinalizeFunc) camel_mapi_store_summary_finalise);
	}

	return type;
}


CamelMapiStoreSummary *
camel_mapi_store_summary_new (void)
{
	CamelMapiStoreSummary *new = CAMEL_MAPI_STORE_SUMMARY ( camel_object_new (camel_mapi_store_summary_get_type ()));

	return new;
}


static int
summary_header_load(CamelStoreSummary *s, FILE *in)
{
	CamelMapiStoreSummary *summary = (CamelMapiStoreSummary *)s ;
	gint32 version;

	if (camel_mapi_store_summary_parent->summary_header_load ((CamelStoreSummary *)s, in) == -1
			|| camel_file_util_decode_fixed_int32(in, &version) == -1)
		return -1 ;

	summary->version = version ;

	return 0 ;
}


static int
summary_header_save(CamelStoreSummary *s, FILE *out)
{

	if (camel_mapi_store_summary_parent->summary_header_save((CamelStoreSummary *)s, out) == -1)
		return -1;

	return 0 ;
}

static CamelStoreInfo *
store_info_load(CamelStoreSummary *s, FILE *in)
{
	CamelMapiStoreInfo *si;

	si = (CamelMapiStoreInfo *)camel_mapi_store_summary_parent->store_info_load(s, in);
	if (si) {
		if (camel_file_util_decode_string(in, &si->full_name) == -1) {
			camel_store_summary_info_free(s, (CamelStoreInfo *)si);
			si = NULL;
		}
	}
	return (CamelStoreInfo *)si;
}

static int
store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *mi)
{
	CamelMapiStoreInfo *summary = (CamelMapiStoreInfo *)mi;
	if (camel_mapi_store_summary_parent->store_info_save(s, out, mi) == -1
	    || camel_file_util_encode_string(out, summary->full_name) == -1) 
		return -1;

	return 0;
}


static void
store_info_free(CamelStoreSummary *s, CamelStoreInfo *mi)
{
	CamelMapiStoreInfo *si = (CamelMapiStoreInfo *)mi;

	g_free(si->full_name);
	camel_mapi_store_summary_parent->store_info_free(s, mi);
}





static const char *
store_info_string(CamelStoreSummary *s, const CamelStoreInfo *mi, int type)
{
	CamelMapiStoreInfo *isi = (CamelMapiStoreInfo *)mi;

	/* FIXME: Locks? */

	g_assert (mi != NULL);

	switch (type) {
		case CAMEL_STORE_INFO_LAST:
			return isi->full_name;
		default:
			return camel_mapi_store_summary_parent->store_info_string(s, mi, type);
	}
}

static void
store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *mi, int type, const char *str)
{
	CamelMapiStoreInfo *isi = (CamelMapiStoreInfo *)mi;

	g_assert(mi != NULL);

	switch(type) {
		case CAMEL_STORE_INFO_LAST:
			d(printf("Set full name %s -> %s\n", isi->full_name, str));
			CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
			g_free(isi->full_name);
			isi->full_name = g_strdup(str);
			CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
			break;
		default:
			camel_mapi_store_summary_parent->store_info_set_string(s, mi, type, str);
			break;
	}
}

CamelMapiStoreInfo *
camel_mapi_store_summary_full_name(CamelMapiStoreSummary *s, const char *full_name)
{
	int count, i;
	CamelMapiStoreInfo *info;

	count = camel_store_summary_count((CamelStoreSummary *)s);
	for (i=0;i<count;i++) {
		info = (CamelMapiStoreInfo *)camel_store_summary_index((CamelStoreSummary *)s, i);
		if (info) {
			if (strcmp(info->full_name, full_name) == 0)
				return info;
			camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		}
	}

	return NULL;

}

char *
camel_mapi_store_summary_full_to_path(CamelMapiStoreSummary *s, const char *full_name, char dir_sep)
{
	char *path, *p;
	int c;
	const char *f;

	if (dir_sep != '/') {
		p = path = alloca(strlen(full_name)*3+1);
		f = full_name;
		while ( (c = *f++ & 0xff) ) {
			if (c == dir_sep)
				*p++ = '/';
//FIXME : why ?? :(
/* 			else if (c == '/' || c == '%') */
/* 				p += sprintf(p, "%%%02X", c); */
			else
				*p++ = c;
		}
		*p = 0;
	} else
		path = (char *)full_name;

	return g_strdup (path);
}


CamelMapiStoreInfo *
camel_mapi_store_summary_add_from_full(CamelMapiStoreSummary *s, const char *full, char dir_sep)
{
	CamelMapiStoreInfo *info;
	char *pathu8;
	int len;
	char *full_name;

	d(printf("adding full name '%s' '%c'\n", full, dir_sep));
	len = strlen(full);
	full_name = alloca(len+1);
	strcpy(full_name, full);

	if (full_name[len-1] == dir_sep)
		full_name[len-1] = 0;

	info = camel_mapi_store_summary_full_name(s, full_name);
	if (info) {
		camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		d(printf("  already there\n"));
		return info;
	}
	pathu8 = camel_mapi_store_summary_full_to_path(s, full_name, '/');
	info = (CamelMapiStoreInfo *)camel_store_summary_add_from_path((CamelStoreSummary *)s, pathu8);
	if (info) 
		camel_store_info_set_string((CamelStoreSummary *)s, (CamelStoreInfo *)info, CAMEL_STORE_INFO_LAST, full_name);

	return info;
}

char *
camel_mapi_store_summary_full_from_path(CamelMapiStoreSummary *s, const char *path)
{
	char *name = NULL;

/* 	ns = camel_mapi_store_summary_namespace_find_path(s, path); */
/* 	if (ns) */
/* 		name = camel_mapi_store_summary_path_to_full(s, path, ns->sep); */

	d(printf("looking up path %s -> %s\n", path, name?name:"not found"));

	return name;
}

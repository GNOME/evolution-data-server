/*
 * Copyright (C) 2005 Novell Inc.
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

#include <stdio.h>
#include <string.h>

#include "camel-mime-utils.h"
#include "camel-nntp-address.h"

#define d(x)

static int    nntp_decode		(CamelAddress *, const char *raw);
static char * nntp_encode		(CamelAddress *);
static int    nntp_cat		(CamelAddress *dest, const CamelAddress *source);
static void   nntp_remove		(CamelAddress *, int index);

static void camel_nntp_address_class_init (CamelNNTPAddressClass *klass);
static void camel_nntp_address_init       (CamelNNTPAddress *obj);

static CamelAddressClass *camel_nntp_address_parent;

struct _address {
	char *name;
	char *address;
};

static void
camel_nntp_address_class_init(CamelNNTPAddressClass *klass)
{
	CamelAddressClass *address = (CamelAddressClass *) klass;

	camel_nntp_address_parent = CAMEL_ADDRESS_CLASS(camel_type_get_global_classfuncs(camel_address_get_type()));

	address->decode = nntp_decode;
	address->encode = nntp_encode;
	address->unformat = nntp_decode;
	address->format = nntp_encode;
	address->remove = nntp_remove;
	address->cat = nntp_cat;
}

static void
camel_nntp_address_init(CamelNNTPAddress *obj)
{
}

CamelType
camel_nntp_address_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_address_get_type(), "CamelNNTPAddress",
					   sizeof (CamelNNTPAddress),
					   sizeof (CamelNNTPAddressClass),
					   (CamelObjectClassInitFunc) camel_nntp_address_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_nntp_address_init,
					   NULL);
	}
	
	return type;
}

/* since newsgropus are 7bit ascii, decode/unformat are the same */
static int
nntp_decode(CamelAddress *a, const char *raw)
{
	struct _camel_header_newsgroup *ha, *n;
	int count = a->addresses->len;

	ha = camel_header_newsgroups_decode(raw);
	if (ha) {
		for (n = ha;n;n=n->next)
			camel_nntp_address_add((CamelNNTPAddress *)a, n->newsgroup);
		camel_header_newsgroups_free(ha);
	}
	
	return a->addresses->len - count;
}

/* since newsgropus are 7bit ascii, encode/format are the same */
static char *
nntp_encode(CamelAddress *a)
{
	int i;
	GString *out;
	char *ret;
	
	if (a->addresses->len == 0)
		return NULL;
	
	out = g_string_new("");
	
	for (i = 0;i < a->addresses->len; i++) {
		if (i != 0)
			g_string_append(out, ", ");

		g_string_append(out, g_ptr_array_index(a->addresses, i));
	}
	
	ret = out->str;
	g_string_free(out, FALSE);
	
	return ret;
}

static int
nntp_cat (CamelAddress *dest, const CamelAddress *source)
{
	int i;

	g_assert(CAMEL_IS_NNTP_ADDRESS(source));

	for (i=0;i<source->addresses->len;i++)
		camel_nntp_address_add((CamelNNTPAddress *)dest, g_ptr_array_index(source->addresses, i));

	return i;
}

static void
nntp_remove	(CamelAddress *a, int index)
{
	if (index < 0 || index >= a->addresses->len)
		return;
	
	g_free(g_ptr_array_index(a->addresses, index));
	g_ptr_array_remove_index(a->addresses, index);
}

/**
 * camel_nntp_address_new:
 *
 * Create a new CamelNNTPAddress object.
 * 
 * Return value: A new CamelNNTPAddress object.
 **/
CamelNNTPAddress *
camel_nntp_address_new (void)
{
	CamelNNTPAddress *new = CAMEL_NNTP_ADDRESS(camel_object_new(camel_nntp_address_get_type()));
	return new;
}

/**
 * camel_nntp_address_add:
 * @a: nntp address object
 * @name: 
 * 
 * Add a new nntp address to the address object.  Duplicates are not added twice.
 * 
 * Return value: Index of added entry, or existing matching entry.
 **/
int
camel_nntp_address_add (CamelNNTPAddress *a, const char *name)
{
	int index, i;

	g_assert(CAMEL_IS_NNTP_ADDRESS(a));

	index = ((CamelAddress *)a)->addresses->len;
	for (i=0;i<index;i++)
		if (!strcmp(g_ptr_array_index(((CamelAddress *)a)->addresses, i), name))
			return i;

	g_ptr_array_add(((CamelAddress *)a)->addresses, g_strdup(name));

	return index;
}

/**
 * camel_nntp_address_get:
 * @a: nntp address object
 * @index: address's array index
 * @addressp: Holder for the returned address, or NULL, if not required.
 * 
 * Get the address at @index.
 * 
 * Return value: TRUE if such an address exists, or FALSE otherwise.
 **/
gboolean
camel_nntp_address_get (const CamelNNTPAddress *a, int index, const char **namep)
{
	g_assert(CAMEL_IS_NNTP_ADDRESS(a));

	if (index < 0 || index >= ((CamelAddress *)a)->addresses->len)
		return FALSE;

	if (namep)
		*namep = g_ptr_array_index( ((CamelAddress *)a)->addresses, index);

	return TRUE;
}

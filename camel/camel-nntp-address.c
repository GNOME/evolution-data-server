/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

#include "camel-mime-utils.h"
#include "camel-nntp-address.h"

#define d(x)

struct _address {
	gchar *name;
	gchar *address;
};

G_DEFINE_TYPE (CamelNNTPAddress, camel_nntp_address, CAMEL_TYPE_ADDRESS)

/* since newsgropus are 7bit ascii, decode/unformat are the same */
static gint
nntp_address_decode (CamelAddress *address,
                     const gchar *raw)
{
	struct _camel_header_newsgroup *ha, *n;
	gint count = address->addresses->len;

	ha = camel_header_newsgroups_decode (raw);
	if (ha) {
		for (n = ha; n; n = n->next)
			camel_nntp_address_add (
				CAMEL_NNTP_ADDRESS (address), n->newsgroup);
		camel_header_newsgroups_free (ha);
	}

	return address->addresses->len - count;
}

/* since newsgropus are 7bit ascii, encode/format are the same */
static gchar *
nntp_address_encode (CamelAddress *address)
{
	gint i;
	GString *out;
	gchar *ret;

	if (address->addresses->len == 0)
		return NULL;

	out = g_string_new ("");

	for (i = 0; i < address->addresses->len; i++) {
		if (i != 0)
			g_string_append (out, ", ");

		g_string_append (out, g_ptr_array_index (address->addresses, i));
	}

	ret = out->str;
	g_string_free (out, FALSE);

	return ret;
}

static gint
nntp_address_cat (CamelAddress *dest,
                  CamelAddress *source)
{
	gint ii;

	g_assert (CAMEL_IS_NNTP_ADDRESS (source));

	for (ii = 0; ii < source->addresses->len; ii++)
		camel_nntp_address_add (
			CAMEL_NNTP_ADDRESS (dest),
			g_ptr_array_index (source->addresses, ii));

	return ii;
}

static void
nntp_address_remove (CamelAddress *address,
                     gint index)
{
	if (index < 0 || index >= address->addresses->len)
		return;

	g_free (g_ptr_array_index (address->addresses, index));
	g_ptr_array_remove_index (address->addresses, index);
}

static void
camel_nntp_address_class_init (CamelNNTPAddressClass *class)
{
	CamelAddressClass *address_class;

	address_class = CAMEL_ADDRESS_CLASS (class);
	address_class->decode = nntp_address_decode;
	address_class->encode = nntp_address_encode;
	address_class->unformat = nntp_address_decode;
	address_class->format = nntp_address_encode;
	address_class->remove = nntp_address_remove;
	address_class->cat = nntp_address_cat;
}

static void
camel_nntp_address_init (CamelNNTPAddress *nntp_address)
{
}

/**
 * camel_nntp_address_new:
 *
 * Create a new CamelNNTPAddress object.
 *
 * Returns: A new CamelNNTPAddress object.
 **/
CamelNNTPAddress *
camel_nntp_address_new (void)
{
	return g_object_new (CAMEL_TYPE_NNTP_ADDRESS, NULL);
}

/**
 * camel_nntp_address_add:
 * @a: nntp address object
 * @name:
 *
 * Add a new nntp address to the address object.  Duplicates are not added twice.
 *
 * Returns: Index of added entry, or existing matching entry.
 **/
gint
camel_nntp_address_add (CamelNNTPAddress *a,
                        const gchar *name)
{
	gint index, i;

	g_assert (CAMEL_IS_NNTP_ADDRESS (a));

	index = ((CamelAddress *) a)->addresses->len;
	for (i = 0; i < index; i++)
		if (!strcmp (g_ptr_array_index (((CamelAddress *) a)->addresses, i), name))
			return i;

	g_ptr_array_add (((CamelAddress *) a)->addresses, g_strdup (name));

	return index;
}

/**
 * camel_nntp_address_get:
 * @a: nntp address object
 * @index: address's array index
 * @namep: Holder for the returned address, or NULL, if not required.
 *
 * Get the address at @index.
 *
 * Returns: TRUE if such an address exists, or FALSE otherwise.
 **/
gboolean
camel_nntp_address_get (CamelNNTPAddress *a,
                        gint index,
                        const gchar **namep)
{
	g_assert (CAMEL_IS_NNTP_ADDRESS (a));

	if (index < 0 || index >= ((CamelAddress *) a)->addresses->len)
		return FALSE;

	if (namep)
		*namep = g_ptr_array_index( ((CamelAddress *)a)->addresses, index);

	return TRUE;
}

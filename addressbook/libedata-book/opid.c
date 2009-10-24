/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#include "opid.h"

G_LOCK_DEFINE_STATIC (lock);
static gint counter = 0;
static GHashTable *hash = NULL;

static void
opid_init (void)
{
	counter = 1;
	hash = g_hash_table_new (g_direct_hash, g_direct_equal);
}

guint32
opid_store (gpointer p)
{
	gint id;

	g_return_val_if_fail (p, 0);

	G_LOCK (lock);

	if (G_UNLIKELY (hash == NULL))
		opid_init ();

	do {
		id = counter++;
	} while (g_hash_table_lookup (hash, GINT_TO_POINTER (id)) != NULL);

	g_hash_table_insert (hash, GINT_TO_POINTER (id), p);

	G_UNLOCK (lock);

	return id;
}

gpointer
opid_fetch (guint32 id)
{
	gpointer p;

	G_LOCK (lock);

	if (G_UNLIKELY (hash == NULL))
		opid_init ();

	p = g_hash_table_lookup (hash, GINT_TO_POINTER (id));

	g_hash_table_remove (hash, GINT_TO_POINTER (id));

	G_UNLOCK (lock);

	return p;
}

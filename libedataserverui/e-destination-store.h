/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-destination-store.h - EDestination store with GtkTreeModel interface.
 *
 * Copyright (C) 2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_DESTINATION_STORE_H
#define E_DESTINATION_STORE_H

#include <gtk/gtktreemodel.h>
#include <libebook/e-destination.h>

G_BEGIN_DECLS

#define E_TYPE_DESTINATION_STORE            (e_destination_store_get_type ())
#define E_DESTINATION_STORE(obj)	    (GTK_CHECK_CAST ((obj), E_TYPE_DESTINATION_STORE, EDestinationStore))
#define E_DESTINATION_STORE_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), E_TYPE_DESTINATION_STORE, EDestinationStoreClass))
#define E_IS_DESTINATION_STORE(obj)         (GTK_CHECK_TYPE ((obj), E_TYPE_DESTINATION_STORE))
#define E_IS_DESTINATION_STORE_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), E_TYPE_DESTINATION_STORE))
#define E_DESTINATION_STORE_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS ((obj), E_TYPE_DESTINATION_STORE, EDestinationStoreClass))

typedef struct EDestinationStore       EDestinationStore;
typedef struct EDestinationStoreClass  EDestinationStoreClass;

struct EDestinationStore {
	GObject     parent;

	/* Private */

	gint        stamp;
	GPtrArray  *destinations;
};

struct EDestinationStoreClass {
	GObjectClass parent_class;
};

typedef enum
{
	E_DESTINATION_STORE_COLUMN_NAME,
	E_DESTINATION_STORE_COLUMN_EMAIL,
	E_DESTINATION_STORE_COLUMN_ADDRESS,

	E_DESTINATION_STORE_NUM_COLUMNS
}
EDestinationStoreColumnType;

GtkType            e_destination_store_get_type           (void);
EDestinationStore *e_destination_store_new                (void);

EDestination      *e_destination_store_get_destination    (EDestinationStore *destination_store,
							   GtkTreeIter *iter);

/* Returns a shallow copy; free the list when done, but don't unref elements */
GList             *e_destination_store_list_destinations  (EDestinationStore *destination_store);

void               e_destination_store_add_destination    (EDestinationStore *destination_store,
							   EDestination *destination);
void               e_destination_store_remove_destination (EDestinationStore *destination_store,
							   EDestination *destination);

G_END_DECLS

#endif  /* E_DESTINATION_STORE_H */

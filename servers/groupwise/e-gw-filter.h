/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 * Sivaiah Nallagatla <snallagatla@novell.com>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef E_GW_FILTER_H
#define E_GW_FILTER_H

#include "soup-soap-message.h"
#include "soup-soap-response.h"

G_BEGIN_DECLS

#define E_TYPE_GW_FILTER           (e_gw_filter_get_type ())
#define E_GW_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_GW_FILTER, EGwFilter))
#define E_GW_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_GW_FILTER, EGwFilterClass))
#define E_IS_GW_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_GW_FILTER))
#define E_IS_GW_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_GW_FILTER))

typedef struct _EGwFilter        EGwFilter;
typedef struct _EGwFilterClass   EGwFilterClass;
typedef struct _EGwFilterPrivate EGwFilterPrivate;

typedef enum {
	E_GW_FILTER_OP_AND,
	E_GW_FILTER_OP_OR,
	E_GW_FILTER_OP_NOT,
	E_GW_FILTER_OP_EQUAL,
	E_GW_FILTER_OP_NOTEQUAL,
	E_GW_FILTER_OP_GREATERTHAN,
	E_GW_FILTER_OP_LESSTHAN,
	E_GW_FILTER_OP_GREATERTHAN_OR_EQUAL,
	E_GW_FILTER_OP_LESSTHAN_OR_EQUAL,
	E_GW_FILTER_OP_CONTAINS,
	E_GW_FILTER_OP_CONTAINSWORD,
	E_GW_FILTER_OP_BEGINS,
	E_GW_FILTER_OP_EXISTS,
	E_GW_FILTER_OP_NOTEXISTS

} EGwFilterOpType;

struct _EGwFilter {
	GObject parent;
	EGwFilterPrivate *priv;
};

struct _EGwFilterClass {
	GObjectClass parent_class;
};

GType       e_gw_filter_get_type (void);
EGwFilter*  e_gw_filter_new(void);
void        e_gw_filter_add_filter_component (EGwFilter *filter, EGwFilterOpType operation, const gchar *field_name, const gchar *field_value);
void        e_gw_filter_append_to_soap_message (EGwFilter *filter, SoupSoapMessage *msg);
void        e_gw_filter_group_conditions (EGwFilter *filter, EGwFilterOpType operation, gint num_of_condtions);

G_END_DECLS

#endif

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef E_GW_CONTAINER_H
#define E_GW_CONTAINER_H

#include <libsoup/soup-soap-response.h>

G_BEGIN_DECLS

#define E_TYPE_GW_CONTAINER            (e_gw_container_get_type ())
#define E_GW_CONTAINER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_GW_CONTAINER, EGwContainer))
#define E_GW_CONTAINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_GW_CONTAINER, EGwContainerClass))
#define E_IS_GW_CONTAINER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_GW_CONTAINER))
#define E_IS_GW_CONTAINER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_GW_CONTAINER))

typedef struct _EGwContainer        EGwContainer;
typedef struct _EGwContainerClass   EGwContainerClass;
typedef struct _EGwContainerPrivate EGwContainerPrivate;

struct _EGwContainer {
	GObject parent;
	EGwContainerPrivate *priv;
};

struct _EGwContainerClass {
	GObjectClass parent_class;
};

GType         e_gw_container_get_type (void);
EGwContainer *e_gw_container_new_from_soap_parameter (SoupSoapParameter *param);
gboolean      e_gw_container_set_from_soap_parameter (EGwContainer *container,
						      SoupSoapParameter *param);
const char   *e_gw_container_get_name (EGwContainer *container);
void          e_gw_container_set_name (EGwContainer *container, const char *new_name);
const char   *e_gw_container_get_id (EGwContainer *container);
void          e_gw_container_set_id (EGwContainer *container, const char *new_id);
gboolean      e_gw_container_get_is_writable (EGwContainer *container);
void          e_gw_container_set_is_writable (EGwContainer *container, gboolean writable);

G_END_DECLS

#endif

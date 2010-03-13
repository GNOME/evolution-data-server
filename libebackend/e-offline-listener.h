/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-interface-check.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 * Author: Sivaiah Nallagatla <snallagatla@novell.com>
 */

#ifndef _E_OFFLINE_LISTENER_H_
#define _E_OFFLINE_LISTENER_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define E_TYPE_OFFLINE_LISTENER			(e_offline_listener_get_type ())
#define E_OFFLINE_LISTENER(obj)			((G_TYPE_CHECK_INSTANCE_CAST((obj), E_TYPE_OFFLINE_LISTENER, EOfflineListener)))
#define E_OFFLINE_LISTENER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), E_TYPE_OFFLINE_LISTENER, EOfflineListenerClass))
#define E_IS_OFFLINE_LISTENER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_OFFLINE_LISTENER))
#define E_IS_OFFLINE_LISTENER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_OFFLINE_LISTENER))

typedef struct _EOfflineListener         EOfflineListener;
typedef struct _EOfflineListenerPrivate  EOfflineListenerPrivate;
typedef struct _EOfflineListenerClass    EOfflineListenerClass;

/**
 * EOfflineListenerState:
 * @EOL_STATE_OFFLINE:
 * @EOL_STATE_ONLINE:
 *
 * Since: 2.30
 **/
typedef enum {
	EOL_STATE_OFFLINE = 0,
	EOL_STATE_ONLINE = 1
} EOfflineListenerState;

/**
 * EOfflineListener:
 *
 * Since: 2.30
 **/
struct _EOfflineListener {
	GObject parent;
	EOfflineListenerPrivate *priv;
};

struct _EOfflineListenerClass {
	GObjectClass  parent_class;

	void (*changed) (EOfflineListener *eol, EOfflineListenerState state);
};

GType e_offline_listener_get_type  (void);

EOfflineListener  *e_offline_listener_new (void);

EOfflineListenerState e_offline_listener_get_state (EOfflineListener *eol);

G_END_DECLS

#endif /* _E_OFFLINE_LISTENER_H_ */

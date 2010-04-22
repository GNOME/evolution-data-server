/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-object.h: Base class for Camel */
/*
 * Authors:
 *  Dan Winship <danw@ximian.com>
 *  Michael Zucchi <notzed@ximian.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_OBJECT_H
#define CAMEL_OBJECT_H

#include <glib-object.h>
#include <stdio.h>		/* FILE */
#include <stdlib.h>		/* gsize */
#include <stdarg.h>

#include <camel/camel-arg.h>
#include <camel/camel-exception.h>

/* Standard GObject macros */
#define CAMEL_TYPE_OBJECT \
	(camel_object_get_type ())
#define CAMEL_OBJECT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_OBJECT, CamelObject))
#define CAMEL_OBJECT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_OBJECT, CamelObjectClass))
#define CAMEL_IS_OBJECT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_OBJECT))
#define CAMEL_IS_OBJECT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_OBJECT))
#define CAMEL_OBJECT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_OBJECT, CamelObjectClass))

G_BEGIN_DECLS

typedef struct _CamelObject CamelObject;
typedef struct _CamelObjectClass CamelObjectClass;
typedef guint CamelObjectHookID;

typedef gboolean (*CamelObjectEventPrepFunc) (CamelObject *, gpointer);
typedef void (*CamelObjectEventHookFunc) (CamelObject *, gpointer, gpointer);

/* camel object args. */
enum {
	/* Get a description of the object. */
	CAMEL_OBJECT_ARG_DESCRIPTION = CAMEL_ARG_FIRST,
	CAMEL_OBJECT_ARG_STATE_FILE,
	CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES
};

enum {
	CAMEL_OBJECT_DESCRIPTION = CAMEL_OBJECT_ARG_DESCRIPTION | CAMEL_ARG_STR,
	/* sets where the persistent data should reside, otherwise it isn't persistent */
	CAMEL_OBJECT_STATE_FILE = CAMEL_OBJECT_ARG_STATE_FILE | CAMEL_ARG_STR,
	/* returns a GSList CamelProperties of persistent properties */
	CAMEL_OBJECT_PERSISTENT_PROPERTIES = CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES
};

struct _CamelObject {
	GObject parent;

	/* current hooks on this object */
	struct _CamelHookList *hooks;
};

struct _CamelObjectClass {
	GObjectClass parent_class;

	/* available hooks for this class */
	struct _CamelHookPair *hooks;

	/* get/set interface */
	gint (*setv)(CamelObject *, CamelException *ex, CamelArgV *args);
	gint (*getv)(CamelObject *, CamelException *ex, CamelArgGetV *args);
	/* we only free 1 at a time, and only pointer types, obviously */
	void (*free)(CamelObject *, guint32 tag, gpointer ptr);

	/* persistence stuff */
	gint (*state_read)(CamelObject *, FILE *fp);
	gint (*state_write)(CamelObject *, FILE *fp);
};

void camel_object_class_add_event (CamelObjectClass *klass, const gchar *name, CamelObjectEventPrepFunc prep);

GType camel_object_get_type (void);

/* hooks */
CamelObjectHookID camel_object_hook_event(gpointer obj, const gchar *name, CamelObjectEventHookFunc hook, gpointer data);
void camel_object_remove_event(gpointer obj, CamelObjectHookID id);
void camel_object_unhook_event(gpointer obj, const gchar *name, CamelObjectEventHookFunc hook, gpointer data);
void camel_object_trigger_event(gpointer obj, const gchar *name, gpointer event_data);

/* get/set methods */
gint camel_object_set(gpointer obj, CamelException *ex, ...);
gint camel_object_setv(gpointer obj, CamelException *ex, CamelArgV *);
gint camel_object_get(gpointer obj, CamelException *ex, ...);
gint camel_object_getv(gpointer obj, CamelException *ex, CamelArgGetV *);

/* reads/writes the state from/to the CAMEL_OBJECT_STATE_FILE */
gint camel_object_state_read(gpointer vo);
gint camel_object_state_write(gpointer vo);

/* free a retrieved object.  May be a noop for static data. */
void camel_object_free(gpointer vo, guint32 tag, gpointer value);

G_END_DECLS

#endif /* CAMEL_OBJECT_H */

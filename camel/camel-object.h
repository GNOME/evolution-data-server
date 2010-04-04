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

#include <glib.h>
#include <stdio.h>		/* FILE */
#include <stdlib.h>		/* gsize */
#include <stdarg.h>
#include <pthread.h>

#include <camel/camel-arg.h>
#include <camel/camel-exception.h>

/* turn on so that camel_object_class_dump_tree() dumps object instances as well */
#define CAMEL_OBJECT_TRACK_INSTANCES

G_BEGIN_DECLS

typedef struct _CamelObjectClass *CamelType;

#ifdef G_DISABLE_CHECKS
#define CAMEL_CHECK_CAST(obj, ctype, ptype)         ((ptype *) obj)
#define CAMEL_CHECK_CLASS_CAST(klass, ctype, ptype) ((ptype *) klass)
#else
#define CAMEL_CHECK_CAST(obj, ctype, ptype)         ((ptype *) camel_object_cast ((CamelObject *)(obj), (CamelType)(ctype)))
#define CAMEL_CHECK_CLASS_CAST(klass, ctype, ptype) ((ptype *) camel_object_class_cast ((CamelObjectClass *)(klass), (CamelType)(ctype) ))
#endif
#define CAMEL_CHECK_TYPE(obj, ctype)                (camel_object_is ((CamelObject *)(obj), (CamelType)(ctype) ))
#define CAMEL_CHECK_CLASS_TYPE(klass, ctype)        (camel_object_class_is ((CamelObjectClass *)(klass), (CamelType)(ctype)))

extern CamelType camel_object_type;

#define CAMEL_TYPE_OBJECT        (camel_object_type)

/* we can't check casts till we've got the type, use the global type variable because its cheaper */
#define CAMEL_OBJECT(obj)        (CAMEL_CHECK_CAST((obj), camel_object_type, CamelObject))
#define CAMEL_OBJECT_CLASS(k)    (CAMEL_CHECK_CLASS_CAST ((k), camel_object_type, CamelObjectClass))
#define CAMEL_IS_OBJECT(o)       (CAMEL_CHECK_TYPE((o), camel_object_type))
#define CAMEL_IS_OBJECT_CLASS(k) (CAMEL_CHECK_CLASS_TYPE((k), camel_object_type))

#define CAMEL_OBJECT_GET_CLASS(o) ((CamelObjectClass *)(CAMEL_OBJECT(o))->klass)
#define CAMEL_OBJECT_GET_TYPE(o)  ((CamelType)(CAMEL_OBJECT(o))->klass)

typedef struct _CamelObjectClass CamelObjectClass;
typedef struct _CamelObject CamelObject;
typedef guint CamelObjectHookID;

typedef void (*CamelObjectClassInitFunc) (CamelObjectClass *);
typedef void (*CamelObjectClassFinalizeFunc) (CamelObjectClass *);
typedef void (*CamelObjectInitFunc) (CamelObject *, CamelObjectClass *);
typedef void (*CamelObjectFinalizeFunc) (CamelObject *);

typedef gboolean (*CamelObjectEventPrepFunc) (CamelObject *, gpointer);
typedef void (*CamelObjectEventHookFunc) (CamelObject *, gpointer, gpointer);

#define CAMEL_INVALID_TYPE (NULL)

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

typedef enum _CamelObjectFlags {
	CAMEL_OBJECT_DESTROY = (1<<0)
} CamelObjectFlags;

/* TODO: create a simpleobject which has no events on it, or an interface for events */
struct _CamelObject {
	struct _CamelObjectClass *klass;

	guint32 magic;		/* only really needed for debugging ... */

	/* current hooks on this object */
	struct _CamelHookList *hooks;

	guint32 ref_count:24;
	guint32 flags:8;

#ifdef CAMEL_OBJECT_TRACK_INSTANCES
	struct _CamelObject *next, *prev;
#endif
};

struct _CamelObjectClass
{
	struct _CamelObjectClass *parent;

	guint32 magic;		/* in same spot for validation */

	struct _CamelObjectClass *next, *child; /* maintain heirarchy, just for kicks */

	const gchar *name;

	gpointer lock;		/* lock when used in threading, else just pads struct */

	/*unsigned short version, revision;*/

	/* if the object's bigger than 64K, it could use redesigning */
	unsigned short object_size/*, object_data*/;
	unsigned short klass_size/*, klass_data*/;

	/* available hooks for this class */
	struct _CamelHookPair *hooks;

	/* memchunks for this type */
	struct _EMemChunk *instance_chunks;
#ifdef CAMEL_OBJECT_TRACK_INSTANCES
	struct _CamelObject *instances;
#endif

	/* init class */
	void (*klass_init)(struct _CamelObjectClass *);
	void (*klass_finalize)(struct _CamelObjectClass *);

	/* init/finalize object */
	void (*init)(struct _CamelObject *, struct _CamelObjectClass *);
	void (*finalize)(struct _CamelObject *);

	/* root-class fields follow, type system above */

	/* get/set interface */
	gint (*setv)(struct _CamelObject *, CamelException *ex, CamelArgV *args);
	gint (*getv)(struct _CamelObject *, CamelException *ex, CamelArgGetV *args);
	/* we only free 1 at a time, and only pointer types, obviously */
	void (*free)(struct _CamelObject *, guint32 tag, gpointer ptr);

	/* persistence stuff */
	gint (*state_read)(struct _CamelObject *, FILE *fp);
	gint (*state_write)(struct _CamelObject *, FILE *fp);
};

/* The type system .... it's pretty simple..... */
void camel_type_init (void);
CamelType camel_type_register(CamelType parent, const gchar * name, /*guint ver, guint rev,*/
			      gsize instance_size,
			      gsize classfuncs_size,
			      CamelObjectClassInitFunc class_init,
			      CamelObjectClassFinalizeFunc  class_finalize,
			      CamelObjectInitFunc instance_init,
			      CamelObjectFinalizeFunc instance_finalize);

/* deprecated interface */
#define camel_type_get_global_classfuncs(x) ((CamelObjectClass *)(x))

/* object class methods (types == classes now) */
const gchar *camel_type_to_name (CamelType type);
CamelType camel_name_to_type (const gchar *name);
void camel_object_class_add_event (CamelObjectClass *klass, const gchar *name, CamelObjectEventPrepFunc prep);

void camel_object_class_dump_tree (CamelType root);

/* casting */
CamelObject *camel_object_cast(CamelObject *obj, CamelType ctype);
gboolean camel_object_is(CamelObject *obj, CamelType ctype);

CamelObjectClass *camel_object_class_cast (CamelObjectClass *klass, CamelType ctype);
gboolean camel_object_class_is (CamelObjectClass *klass, CamelType ctype);

CamelType camel_object_get_type (void);

CamelObject *camel_object_new (CamelType type);

void camel_object_ref(gpointer);
void camel_object_unref(gpointer);

#ifdef CAMEL_DEBUG
#define camel_object_ref(o) (printf("%s (%s:%d):ref (%p)\n", __FUNCTION__, __FILE__, __LINE__, o), camel_object_ref(o))
#define camel_object_unref(o) (printf("%s (%s:%d):unref (%p)\n", __FUNCTION__, __FILE__, __LINE__, o), camel_object_unref (o))
#endif

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

/* for managing bags of weakly-ref'd 'child' objects */
typedef struct _CamelObjectBag CamelObjectBag;
typedef gpointer (*CamelCopyFunc)(gconstpointer vo);

CamelObjectBag *camel_object_bag_new(GHashFunc hash, GEqualFunc equal, CamelCopyFunc keycopy, GFreeFunc keyfree);
gpointer camel_object_bag_get(CamelObjectBag *bag, gconstpointer key);
gpointer camel_object_bag_peek(CamelObjectBag *bag, gconstpointer key);
gpointer camel_object_bag_reserve(CamelObjectBag *bag, gconstpointer key);
void camel_object_bag_add(CamelObjectBag *bag, gconstpointer key, gpointer vo);
void camel_object_bag_abort(CamelObjectBag *bag, gconstpointer key);
void camel_object_bag_rekey(CamelObjectBag *bag, gpointer o, gconstpointer newkey);
GPtrArray *camel_object_bag_list(CamelObjectBag *bag);
void camel_object_bag_remove(CamelObjectBag *bag, gpointer o);
void camel_object_bag_destroy(CamelObjectBag *bag);

G_END_DECLS

#endif /* CAMEL_OBJECT_H */

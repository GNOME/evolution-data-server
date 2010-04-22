/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *
 *
 * Author:
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>

#include "camel-file-utils.h"
#include "camel-object.h"

#define d(x)
#define b(x)			/* object bag */
#define h(x)			/* hooks */

G_DEFINE_ABSTRACT_TYPE (CamelObject, camel_object, G_TYPE_OBJECT)

/* ** Quickie type system ************************************************* */

/* A 'locked' hooklist, that is only allocated on demand */
typedef struct _CamelHookList {
	GStaticRecMutex lock;

	guint depth:30;	/* recursive event depth */
	guint flags:2;	/* flags, see below */

	guint list_length;
	struct _CamelHookPair *list;
} CamelHookList;

#define CAMEL_HOOK_PAIR_REMOVED (1<<0)

/* a 'hook pair', actually a hook tuple, we just store all hooked events in the same list,
   and just comapre as we go, rather than storing separate lists for each hook type

   the name field just points directly to the key field in the class's preplist hashtable.
   This way we can just use a direct pointer compare when scanning it, and also saves
   copying the string */
typedef struct _CamelHookPair
{
	struct _CamelHookPair *next; /* next MUST be the first member */

	guint id:30;
	guint flags:2;	/* removed, etc */

	const gchar *name;	/* points to the key field in the classes preplist, static memory */
	union {
		CamelObjectEventHookFunc event;
		CamelObjectEventPrepFunc prep;
		gchar *filename;
	} func;
	gpointer data;
} CamelHookPair;

/* meta-data stuff */
static CamelHookPair *co_metadata_pair(CamelObject *obj, gint create);

static const gchar meta_name[] = "object:meta";
#define CAMEL_OBJECT_STATE_FILE_MAGIC "CLMD"

/* ********************************************************************** */

static CamelHookList *camel_object_get_hooks(CamelObject *o);
static void camel_object_free_hooks(CamelObject *o);

#define camel_object_unget_hooks(o) \
	(g_static_rec_mutex_unlock(&CAMEL_OBJECT(o)->hooks->lock))

/* ********************************************************************** */

#define CLASS_LOCK(k) (g_mutex_lock((((CamelObjectClass *)k)->lock)))
#define CLASS_UNLOCK(k) (g_mutex_unlock((((CamelObjectClass *)k)->lock)))
#define REF_LOCK() (g_mutex_lock(ref_lock))
#define REF_UNLOCK() (g_mutex_unlock(ref_lock))
#define TYPE_LOCK() (g_static_rec_mutex_lock(&type_lock))
#define TYPE_UNLOCK() (g_static_rec_mutex_unlock(&type_lock))

static struct _CamelHookPair *
pair_alloc(void)
{
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
	static guint next_id = 1;
	CamelHookPair *pair;

	pair = g_slice_new (CamelHookPair);

	g_static_mutex_lock (&mutex);
	pair->id = next_id++;
	if (next_id == 0)
		next_id = 1;
	g_static_mutex_unlock (&mutex);

	return pair;
}

static void
pair_free(CamelHookPair *pair)
{
	g_slice_free (CamelHookPair, pair);
}

static struct _CamelHookList *
hooks_alloc(void)
{
	return g_slice_new0 (CamelHookList);
}

static void
hooks_free(CamelHookList *hooks)
{
	g_slice_free (CamelHookList, hooks);
}

/* ************************************************************************ */

/* CamelObject base methods */

static gint
cobject_getv (CamelObject *o,
              CamelException *ex,
              CamelArgGetV *args)
{
	CamelObjectClass *class;
	gint i;
	guint32 tag;

	class = CAMEL_OBJECT_GET_CLASS (o);

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OBJECT_ARG_DESCRIPTION:
			*arg->ca_str = (gchar *) G_OBJECT_CLASS_NAME (class);
			break;
		case CAMEL_OBJECT_ARG_STATE_FILE: {
			CamelHookPair *pair = co_metadata_pair(o, FALSE);

			if (pair) {
				*arg->ca_str = g_strdup(pair->func.filename);
				camel_object_unget_hooks(o);
			}
			break; }
		}
	}

	/* could have flags or stuff here? */
	return 0;
}

static gint
cobject_setv (CamelObject *o,
              CamelException *ex,
              CamelArgV *args)
{
	gint i;
	guint32 tag;

	for (i=0;i<args->argc;i++) {
		CamelArg *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OBJECT_ARG_STATE_FILE: {
			CamelHookPair *pair;

			/* We store the filename on the meta-data hook-pair */
			pair = co_metadata_pair(o, TRUE);
			g_free(pair->func.filename);
			pair->func.filename = g_strdup(arg->ca_str);
			camel_object_unget_hooks(o);
			break; }
		}
	}

	/* could have flags or stuff here? */
	return 0;
}

static void
cobject_free(CamelObject *o, guint32 tag, gpointer value)
{
	switch (tag & CAMEL_ARG_TAG) {
	case CAMEL_OBJECT_ARG_STATE_FILE:
		g_free(value);
		break;
	}
}

/* State file for CamelObject data.  Any later versions should only append data.

   version:uint32

   Version 0 of the file:

   version:uint32 = 0
   count:uint32				-- count of meta-data items
   ( name:string value:string ) *count		-- meta-data items

   Version 1 of the file adds:
   count:uint32					-- count of persistent properties
   ( tag:uing32 value:tagtype ) *count		-- persistent properties

*/

static gint
cobject_state_read(CamelObject *obj, FILE *fp)
{
	guint32 i, count, version;

	/* NB: for later versions, just check the version is 1 .. known version */
	if (camel_file_util_decode_uint32(fp, &version) == -1
	    || version > 1
	    || camel_file_util_decode_uint32(fp, &count) == -1)
		return -1;

	for (i=0;i<count;i++) {
		gchar *name = NULL, *value = NULL;

		if (camel_file_util_decode_string(fp, &name) == 0
		    && camel_file_util_decode_string(fp, &value) == 0) {
			/* XXX This no longer does anything.
			 *     We're just eating dead data. */
			g_free(name);
			g_free(value);
		} else {
			g_free(name);
			g_free(value);

			return -1;
		}
	}

	if (version > 0) {
		CamelArgV *argv;

		if (camel_file_util_decode_uint32(fp, &count) == -1
			|| count == 0 || count > 1024) {
			/* maybe it was just version 0 afterall */
			return 0;
		}

		count = MIN(count, CAMEL_ARGV_MAX);

		/* we batch up the properties and set them in one go */
		argv = g_try_malloc(sizeof(CamelArgV) -
			((CAMEL_ARGV_MAX - count) * sizeof(CamelArg)));
		if (argv == NULL)
			return -1;

		argv->argc = 0;
		for (i=0;i<count;i++) {
			if (camel_file_util_decode_uint32(fp, &argv->argv[argv->argc].tag) == -1)
				goto cleanup;

			/* so far,only do strings and ints, doubles could be added,
			   object's would require a serialisation interface */

			switch (argv->argv[argv->argc].tag & CAMEL_ARG_TYPE) {
			case CAMEL_ARG_INT:
			case CAMEL_ARG_BOO:
				if (camel_file_util_decode_uint32(fp, (guint32 *) &argv->argv[argv->argc].ca_int) == -1)
					goto cleanup;
				break;
			case CAMEL_ARG_STR:
				if (camel_file_util_decode_string(fp, &argv->argv[argv->argc].ca_str) == -1)
					goto cleanup;
				break;
			default:
				goto cleanup;
			}

			argv->argc++;
		}

		camel_object_setv(obj, NULL, argv);
	cleanup:
		for (i=0;i<argv->argc;i++) {
			if ((argv->argv[i].tag & CAMEL_ARG_TYPE) == CAMEL_ARG_STR)
				g_free(argv->argv[i].ca_str);
		}
		g_free(argv);
	}

	return 0;
}

/* TODO: should pass exception around */
static gint
cobject_state_write(CamelObject *obj, FILE *fp)
{
	gint32 count, i;
	gint res = -1;
	GSList *props = NULL, *l;
	CamelArgGetV *arggetv = NULL;
	CamelArgV *argv = NULL;

	/* current version is 1 */
	if (camel_file_util_encode_uint32(fp, 1) == -1
	    || camel_file_util_encode_uint32(fp, 0) == -1)
		goto abort;

	camel_object_get(obj, NULL, CAMEL_OBJECT_PERSISTENT_PROPERTIES, &props, NULL);

	/* we build an arggetv to query the object atomically,
	   we also need an argv to store the results - bit messy */

	count = g_slist_length(props);
	count = MIN(count, CAMEL_ARGV_MAX);

	arggetv = g_malloc0(sizeof(CamelArgGetV) -
		((CAMEL_ARGV_MAX - count) * sizeof(CamelArgGet)));
	argv = g_malloc0(sizeof(CamelArgV) -
		((CAMEL_ARGV_MAX - count) * sizeof(CamelArg)));
	l = props;
	i = 0;
	while (l) {
		CamelProperty *prop = l->data;

		argv->argv[i].tag = prop->tag;
		arggetv->argv[i].tag = prop->tag;
		arggetv->argv[i].ca_ptr = &argv->argv[i].ca_ptr;

		i++;
		l = l->next;
	}
	arggetv->argc = i;
	argv->argc = i;

	camel_object_getv(obj, NULL, arggetv);

	if (camel_file_util_encode_uint32(fp, count) == -1)
		goto abort;

	for (i=0;i<argv->argc;i++) {
		CamelArg *arg = &argv->argv[i];

		if (camel_file_util_encode_uint32(fp, arg->tag) == -1)
			goto abort;

		switch (arg->tag & CAMEL_ARG_TYPE) {
		case CAMEL_ARG_INT:
		case CAMEL_ARG_BOO:
			if (camel_file_util_encode_uint32(fp, arg->ca_int) == -1)
				goto abort;
			break;
		case CAMEL_ARG_STR:
			if (camel_file_util_encode_string(fp, arg->ca_str) == -1)
				goto abort;
			break;
		}
	}

	res = 0;
abort:
	for (i=0;i<argv->argc;i++) {
		CamelArg *arg = &argv->argv[i];

		if ((argv->argv[i].tag & CAMEL_ARG_TYPE) == CAMEL_ARG_STR)
			camel_object_free(obj, arg->tag, arg->ca_str);
	}

	g_free(argv);
	g_free(arggetv);

	if (props)
		camel_object_free(obj, CAMEL_OBJECT_PERSISTENT_PROPERTIES, props);

	return res;
}

static void
object_dispose (GObject *object)
{
	CamelObject *camel_object = CAMEL_OBJECT (object);

	if (camel_object->hooks != NULL) {
		camel_object_trigger_event (object, "finalize", NULL);
		camel_object_free_hooks (camel_object);
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_object_parent_class)->dispose (object);
}

static void
camel_object_class_init (CamelObjectClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = object_dispose;

	class->hooks = NULL;

	class->getv = cobject_getv;
	class->setv = cobject_setv;
	class->free = cobject_free;

	class->state_read = cobject_state_read;
	class->state_write = cobject_state_write;

	camel_object_class_add_event (class, "finalize", NULL);
}

static void
camel_object_init (CamelObject *object)
{
}

static CamelHookPair *
co_find_pair (CamelObjectClass *class,
              const gchar *name)
{
	CamelHookPair *hook;

	hook = class->hooks;
	while (hook) {
		if (strcmp (hook->name, name) == 0)
			return hook;
		hook = hook->next;
	}

	return NULL;
}

/* class functions */
void
camel_object_class_add_event (CamelObjectClass *class,
                              const gchar *name,
                              CamelObjectEventPrepFunc prep)
{
	CamelHookPair *pair;

	g_return_if_fail (name);

	pair = co_find_pair (class, name);
	if (pair) {
		g_warning ("%s: '%s' is already declared for '%s'",
			G_STRFUNC, name, G_OBJECT_CLASS_NAME (class));
		return;
	}

	pair = pair_alloc ();
	pair->name = name;
	pair->func.prep = prep;
	pair->flags = 0;

	pair->next = class->hooks;
	class->hooks = pair;
}

/* free hook data */
static void
camel_object_free_hooks(CamelObject *o)
{
        CamelHookPair *pair, *next;

        if (o->hooks) {
                g_assert(o->hooks->depth == 0);
                g_assert((o->hooks->flags & CAMEL_HOOK_PAIR_REMOVED) == 0);

                pair = o->hooks->list;
                while (pair) {
                        next = pair->next;

                        pair_free(pair);
                        pair = next;
                }
                g_static_rec_mutex_free(&o->hooks->lock);
                hooks_free(o->hooks);
                o->hooks = NULL;
        }
}

/* return (allocate if required) the object's hook list, locking at the same time */
static CamelHookList *
camel_object_get_hooks(CamelObject *o)
{
	static GStaticMutex lock = G_STATIC_MUTEX_INIT;
	CamelHookList *hooks;

	/* if we have it, we dont have to do any other locking,
	   otherwise use a global lock to setup the object's hook data */
	if (o->hooks == NULL) {
		g_static_mutex_lock(&lock);
		if (o->hooks == NULL) {
			hooks = hooks_alloc();
			g_static_rec_mutex_init(&hooks->lock);
			hooks->flags = 0;
			hooks->depth = 0;
			hooks->list_length = 0;
			hooks->list = NULL;
			o->hooks = hooks;
		}
		g_static_mutex_unlock(&lock);
	}

	g_static_rec_mutex_lock(&o->hooks->lock);

	return o->hooks;
}

guint
camel_object_hook_event (gpointer vo,
                         const gchar *name,
                         CamelObjectEventHookFunc func,
                         gpointer data)
{
	CamelObject *obj = vo;
	CamelObjectClass *class;
	CamelHookPair *pair, *hook;
	CamelHookList *hooks;
	gint id;

	g_return_val_if_fail(CAMEL_IS_OBJECT (obj), 0);
	g_return_val_if_fail(name != NULL, 0);
	g_return_val_if_fail(func != NULL, 0);

	class = CAMEL_OBJECT_GET_CLASS (obj);

	hook = co_find_pair(class, name);

	/* Check all interfaces on this object for events defined on them */
	if (hook == NULL) {
		g_warning("camel_object_hook_event: trying to hook event '%s' in class '%s' with no defined events.",
			  name, G_OBJECT_CLASS_NAME (class));

		return 0;
	}

	/* setup hook pair */
	pair = pair_alloc();
	pair->name = hook->name;	/* effectively static! */
	pair->func.event = func;
	pair->data = data;
	pair->flags = 0;
	id = pair->id;

	/* get the hook list object, locked, link in new event hook, unlock */
	hooks = camel_object_get_hooks(obj);
	pair->next = hooks->list;
	hooks->list = pair;
	hooks->list_length++;
	camel_object_unget_hooks(obj);

	h(printf("%p hook event '%s' %p %p = %d\n", vo, name, func, data, id));

	return id;
}

void
camel_object_remove_event (gpointer vo,
                           guint id)
{
	CamelObject *obj = vo;
	CamelHookList *hooks;
	CamelHookPair *pair, *parent;

	g_return_if_fail (CAMEL_IS_OBJECT (obj));
	g_return_if_fail (id != 0);

	if (obj->hooks == NULL) {
		g_warning("camel_object_unhook_event: trying to unhook '%u' from an instance of '%s' with no hooks",
			  id, G_OBJECT_TYPE_NAME (obj));
		return;
	}

	h(printf("%p remove event %d\n", vo, id));

	/* scan hooks for this event, remove it, or flag it if we're busy */
	hooks = camel_object_get_hooks(obj);
	parent = (CamelHookPair *)&hooks->list;
	pair = parent->next;
	while (pair) {
		if (pair->id == id
		    && (pair->flags & CAMEL_HOOK_PAIR_REMOVED) == 0) {
			if (hooks->depth > 0) {
				pair->flags |= CAMEL_HOOK_PAIR_REMOVED;
				hooks->flags |= CAMEL_HOOK_PAIR_REMOVED;
			} else {
				parent->next = pair->next;
				pair_free(pair);
				hooks->list_length--;
			}
			camel_object_unget_hooks(obj);
			return;
		}
		parent = pair;
		pair = pair->next;
	}
	camel_object_unget_hooks(obj);

	g_warning("camel_object_unhook_event: cannot find hook id %u in instance of '%s'",
		  id, G_OBJECT_TYPE_NAME (obj));
}

void
camel_object_unhook_event (gpointer vo,
                           const gchar *name,
                           CamelObjectEventHookFunc func,
                           gpointer data)
{
	CamelObject *obj = vo;
	CamelHookList *hooks;
	CamelHookPair *pair, *parent;

	g_return_if_fail (CAMEL_IS_OBJECT (obj));
	g_return_if_fail (name != NULL);
	g_return_if_fail (func != NULL);

	if (obj->hooks == NULL) {
		g_warning("camel_object_unhook_event: trying to unhook '%s' from an instance of '%s' with no hooks",
			  name, G_OBJECT_TYPE_NAME (obj));
		return;
	}

	h(printf("%p unhook event '%s' %p %p\n", vo, name, func, data));

	/* scan hooks for this event, remove it, or flag it if we're busy */
	hooks = camel_object_get_hooks(obj);
	parent = (CamelHookPair *)&hooks->list;
	pair = parent->next;
	while (pair) {
		if (pair->func.event == func
		    && pair->data == data
		    && strcmp(pair->name, name) == 0
		    && (pair->flags & CAMEL_HOOK_PAIR_REMOVED) == 0) {
			if (hooks->depth > 0) {
				pair->flags |= CAMEL_HOOK_PAIR_REMOVED;
				hooks->flags |= CAMEL_HOOK_PAIR_REMOVED;
			} else {
				parent->next = pair->next;
				pair_free(pair);
				hooks->list_length--;
			}
			camel_object_unget_hooks(obj);
			return;
		}
		parent = pair;
		pair = pair->next;
	}
	camel_object_unget_hooks(obj);

	g_warning("camel_object_unhook_event: cannot find hook/data pair %p/%p in an instance of '%s' attached to '%s'",
		  (gpointer) func, data, G_OBJECT_TYPE_NAME (obj), name);
}

void
camel_object_trigger_event (gpointer vo,
                            const gchar *name,
                            gpointer event_data)
{
	CamelObject *obj = vo;
	CamelObjectClass *class;
	CamelHookList *hooks;
	CamelHookPair *pair, **pairs, *parent, *hook;
	gint i, size;
	const gchar *prepname;

	g_return_if_fail (CAMEL_IS_OBJECT (obj));
	g_return_if_fail (name);

	class = CAMEL_OBJECT_GET_CLASS (obj);

	hook = co_find_pair(class, name);
	if (hook)
		goto trigger;

	if (obj->hooks == NULL)
		return;

	g_warning("camel_object_trigger_event: trying to trigger unknown event '%s' in class '%s'",
		  name, G_OBJECT_TYPE_NAME (obj));

	return;

trigger:
	/* try prep function, if false, then quit */
	if (hook->func.prep != NULL && !hook->func.prep(obj, event_data))
		return;

	/* also, no hooks, dont bother going further */
	if (obj->hooks == NULL)
		return;

	/* lock the object for hook emission */
	g_object_ref(obj);
	hooks = camel_object_get_hooks(obj);

	if (hooks->list) {
		/* first, copy the items in the list, and say we're in an event */
		hooks->depth++;
		pair = hooks->list;
		size = 0;
		pairs = alloca(sizeof(pairs[0]) * hooks->list_length);
		prepname = hook->name;
		while (pair) {
			if (pair->name == prepname)
				pairs[size++] = pair;
			pair = pair->next;
		}

		/* now execute the events we have, if they haven't been removed during our calls */
		for (i=size-1;i>=0;i--) {
			pair = pairs[i];
			if ((pair->flags & CAMEL_HOOK_PAIR_REMOVED) == 0)
				(pair->func.event) (obj, event_data, pair->data);
		}
		hooks->depth--;

		/* and if we're out of any events, then clean up any pending removes */
		if (hooks->depth == 0 && (hooks->flags & CAMEL_HOOK_PAIR_REMOVED)) {
			parent = (CamelHookPair *)&hooks->list;
			pair = parent->next;
			while (pair) {
				if (pair->flags & CAMEL_HOOK_PAIR_REMOVED) {
					parent->next = pair->next;
					pair_free(pair);
					hooks->list_length--;
				} else {
					parent = pair;
				}
				pair = parent->next;
			}
			hooks->flags &= ~CAMEL_HOOK_PAIR_REMOVED;
		}
	}

	camel_object_unget_hooks(obj);
	g_object_unref(obj);
}

/* get/set arg methods */
gint
camel_object_set (gpointer vo,
                  CamelException *ex,
                  ...)
{
	CamelObjectClass *class;
	CamelArgV args;
	CamelObject *o = vo;
	gint ret = 0;

	g_return_val_if_fail(CAMEL_IS_OBJECT(o), -1);

	camel_argv_start(&args, ex);

	class = CAMEL_OBJECT_GET_CLASS (o);
	g_return_val_if_fail (class->setv != NULL, -1);

	while (camel_argv_build(&args) && ret == 0)
		ret = class->setv(o, ex, &args);
	if (ret == 0)
		ret = class->setv(o, ex, &args);

	camel_argv_end(&args);

	return ret;
}

gint
camel_object_setv (gpointer vo,
                   CamelException *ex,
                   CamelArgV *args)
{
	CamelObjectClass *class;

	g_return_val_if_fail(CAMEL_IS_OBJECT(vo), -1);

	class = CAMEL_OBJECT_GET_CLASS (vo);
	g_return_val_if_fail (class->setv != NULL, -1);

	return class->setv (vo, ex, args);
}

gint
camel_object_get (gpointer vo,
                  CamelException *ex,
                  ...)
{
	CamelObjectClass *class;
	CamelObject *o = vo;
	CamelArgGetV args;
	gint ret = 0;

	g_return_val_if_fail(CAMEL_IS_OBJECT(o), -1);

	camel_argv_start(&args, ex);

	class = CAMEL_OBJECT_GET_CLASS (o);
	g_return_val_if_fail (class->getv != NULL, -1);

	while (camel_arggetv_build(&args) && ret == 0)
		ret = class->getv(o, ex, &args);
	if (ret == 0)
		ret = class->getv(o, ex, &args);

	camel_argv_end(&args);

	return ret;
}

gint
camel_object_getv (gpointer vo,
                   CamelException *ex,
                   CamelArgGetV *args)
{
	CamelObjectClass *class;

	g_return_val_if_fail(CAMEL_IS_OBJECT(vo), -1);

	class = CAMEL_OBJECT_GET_CLASS (vo);
	g_return_val_if_fail (class->getv != NULL, -1);

	return class->getv (vo, ex, args);
}

/* NB: If this doesn't return NULL, then you must unget_hooks when done */
static CamelHookPair *
co_metadata_pair(CamelObject *obj, gint create)
{
	CamelHookPair *pair;
	CamelHookList *hooks;

	if (obj->hooks == NULL && !create)
		return NULL;

	hooks = camel_object_get_hooks(obj);
	pair = hooks->list;
	while (pair) {
		if (pair->name == meta_name)
			return pair;

		pair = pair->next;
	}

	if (create) {
		pair = pair_alloc();
		pair->name = meta_name;
		pair->data = NULL;
		pair->flags = 0;
		pair->func.filename = NULL;
		pair->next = hooks->list;
		hooks->list = pair;
		hooks->list_length++;
	} else {
		camel_object_unget_hooks(obj);
	}

	return pair;
}

/**
 * camel_object_state_read:
 * @vo:
 *
 * Read persistent object state from object_set(CAMEL_OBJECT_STATE_FILE).
 *
 * Returns: -1 on error.
 **/
gint camel_object_state_read(gpointer vo)
{
	CamelObject *obj = vo;
	gint res = -1;
	gchar *file;
	FILE *fp;
	gchar magic[4];

	camel_object_get(vo, NULL, CAMEL_OBJECT_STATE_FILE, &file, NULL);
	if (file == NULL)
		return 0;

	fp = g_fopen(file, "rb");
	if (fp != NULL) {
		if (fread(magic, 4, 1, fp) == 1
		    && memcmp(magic, CAMEL_OBJECT_STATE_FILE_MAGIC, 4) == 0)
			res = CAMEL_OBJECT_GET_CLASS (obj)->state_read(obj, fp);
		else
			res = -1;
		fclose(fp);
	}

	camel_object_free(vo, CAMEL_OBJECT_STATE_FILE, file);

	return res;
}

/**
 * camel_object_state_write:
 * @vo:
 *
 * Write persistent state to the file as set by object_set(CAMEL_OBJECT_STATE_FILE).
 *
 * Returns: -1 on error.
 **/
gint camel_object_state_write(gpointer vo)
{
	CamelObject *obj = vo;
	gint res = -1;
	gchar *file, *savename, *dirname;
	FILE *fp;

	camel_object_get(vo, NULL, CAMEL_OBJECT_STATE_FILE, &file, NULL);
	if (file == NULL)
		return 0;

	savename = camel_file_util_savename(file);
	dirname = g_path_get_dirname(savename);
	g_mkdir_with_parents(dirname, 0700);
	g_free(dirname);
	fp = g_fopen(savename, "wb");
	if (fp != NULL) {
		if (fwrite(CAMEL_OBJECT_STATE_FILE_MAGIC, 4, 1, fp) == 1
		    && CAMEL_OBJECT_GET_CLASS (obj)->state_write(obj, fp) == 0) {
			if (fclose(fp) == 0) {
				res = 0;
				g_rename(savename, file);
			}
		} else {
			fclose(fp);
		}
	} else {
		g_warning("Could not save object state file to '%s': %s", savename, g_strerror(errno));
	}

	g_free(savename);
	camel_object_free(vo, CAMEL_OBJECT_STATE_FILE, file);

	return res;
}

/* free an arg object, you can only free objects 1 at a time */
void camel_object_free(gpointer vo, guint32 tag, gpointer value)
{
	g_return_if_fail(CAMEL_IS_OBJECT(vo));

	/* We could also handle freeing of args differently

	   Add a 'const' bit to the arg type field,
	   specifying that the object should not be freed.

	   And, add free handlers for any pointer objects which are
	   not const.  The free handlers would be hookpairs off of the
	   class.

	   Then we handle the basic types OBJ,STR here, and pass PTR
	   types to their appropriate handler, without having to pass
	   through the invocation heirarchy of the free method.

	   This would also let us copy and do other things with args
	   we can't do, but i can't see a use for that yet ...  */

	CAMEL_OBJECT_GET_CLASS (vo)->free (vo, tag, value);
}

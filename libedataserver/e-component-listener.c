/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Component listener.
 *
 * Author:
 *   Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-object.h>
#include "e-component-listener.h"

struct _EComponentListenerPrivate {
	Bonobo_Unknown component;
};

static void e_component_listener_finalize   (GObject *object);

static GList *watched_connections = NULL;
static GStaticRecMutex watched_lock = G_STATIC_REC_MUTEX_INIT;

enum {
	COMPONENT_DIED,
	LAST_SIGNAL
};

static guint comp_listener_signals[LAST_SIGNAL];

static void
connection_listen_cb (gpointer object, gpointer user_data)
{
	GList *l, *next = NULL;
	EComponentListener *cl;

	g_static_rec_mutex_lock (&watched_lock);

	for (l = watched_connections; l != NULL; l = next) {
		next = l->next;
		cl = l->data;

		switch (ORBit_small_get_connection_status (cl->priv->component)) {
		case ORBIT_CONNECTION_DISCONNECTED :
			watched_connections = g_list_delete_link (watched_connections, l);

			g_object_ref (cl);
			g_signal_emit (cl, comp_listener_signals[COMPONENT_DIED], 0);
			cl->priv->component = CORBA_OBJECT_NIL;
			g_object_unref (cl);
			break;
		default :
			break;
		}
	}

	g_static_rec_mutex_unlock (&watched_lock);
}

G_DEFINE_TYPE (EComponentListener, e_component_listener, G_TYPE_OBJECT);

static void
e_component_listener_class_init (EComponentListenerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = e_component_listener_finalize;
	klass->component_died = NULL;

	comp_listener_signals[COMPONENT_DIED] =
		g_signal_new ("component_died",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EComponentListenerClass, component_died),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
e_component_listener_init (EComponentListener *cl)
{
	/* allocate internal structure */
	cl->priv = g_new (EComponentListenerPrivate, 1);
	cl->priv->component = CORBA_OBJECT_NIL;
}

static void
e_component_listener_finalize (GObject *object)
{
	EComponentListener *cl = (EComponentListener *) object;

	g_return_if_fail (E_IS_COMPONENT_LISTENER (cl));

	g_static_rec_mutex_lock (&watched_lock);
	watched_connections = g_list_remove (watched_connections, cl);
	g_static_rec_mutex_unlock (&watched_lock);

	if (cl->priv->component != CORBA_OBJECT_NIL)
		cl->priv->component = CORBA_OBJECT_NIL;

	/* free memory */
	g_free (cl->priv);
	cl->priv = NULL;

	if (G_OBJECT_CLASS (e_component_listener_parent_class)->finalize)
		(* G_OBJECT_CLASS (e_component_listener_parent_class)->finalize) (object);
}

/**
 * e_component_listener_new
 * @comp: Component to listen for.
 *
 * Create a new #EComponentListener object, which allows to listen
 * for a given component and get notified when that component dies.
 *
 * Returns: a component listener object.
 */
EComponentListener *
e_component_listener_new (Bonobo_Unknown comp)
{
	EComponentListener *cl;

	g_return_val_if_fail (comp != NULL, NULL);

	g_static_rec_mutex_lock (&watched_lock);

	cl = g_object_new (E_COMPONENT_LISTENER_TYPE, NULL);
	cl->priv->component = comp;

	/* watch the connection */
	ORBit_small_listen_for_broken (comp, G_CALLBACK (connection_listen_cb), cl);
	watched_connections = g_list_prepend (watched_connections, cl);

	g_static_rec_mutex_unlock (&watched_lock);

	return cl;
}

Bonobo_Unknown
e_component_listener_get_component (EComponentListener *cl)
{
	g_return_val_if_fail (E_IS_COMPONENT_LISTENER (cl), CORBA_OBJECT_NIL);
	return cl->priv->component;
}

void
e_component_listener_set_component (EComponentListener *cl, Bonobo_Unknown comp)
{
	g_return_if_fail (E_IS_COMPONENT_LISTENER (cl));

	cl->priv->component = comp;
	ORBit_small_listen_for_broken (comp, G_CALLBACK (connection_listen_cb), cl);
}

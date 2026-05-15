/*
 * SPDX-FileCopyrightText: (C) 2017 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_REGISTRY_WATCHER_H
#define E_SOURCE_REGISTRY_WATCHER_H

#include <libedataserver/e-source-registry.h>
#include <libedataserver/e-source.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_REGISTRY_WATCHER \
	(e_source_registry_watcher_get_type ())
#define E_SOURCE_REGISTRY_WATCHER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_REGISTRY_WATCHER, ESourceRegistryWatcher))
#define E_SOURCE_REGISTRY_WATCHER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_REGISTRY_WATCHER, ESourceRegistryWatcherClass))
#define E_IS_SOURCE_REGISTRY_WATCHER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_REGISTRY_WATCHER))
#define E_IS_SOURCE_REGISTRY_WATCHER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_REGISTRY_WATCHER))
#define E_SOURCE_REGISTRY_WATCHER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_REGISTRY_WATCHER, ESourceRegistryWatcherClass))

G_BEGIN_DECLS

typedef struct _ESourceRegistryWatcher ESourceRegistryWatcher;
typedef struct _ESourceRegistryWatcherClass ESourceRegistryWatcherClass;
typedef struct _ESourceRegistryWatcherPrivate ESourceRegistryWatcherPrivate;

/**
 * ESourceRegistryWatcher:
 **/
struct _ESourceRegistryWatcher {
	/*< private >*/
	GObject parent;
	ESourceRegistryWatcherPrivate *priv;
};

struct _ESourceRegistryWatcherClass {
	GObjectClass parent_class;

	/* Signals */
	gboolean	(* filter)	(ESourceRegistryWatcher *watcher,
					 ESource *source);
	void		(* appeared)	(ESourceRegistryWatcher *watcher,
					 ESource *source);
	void		(* disappeared)	(ESourceRegistryWatcher *watcher,
					 ESource *source);
};

GType		e_source_registry_watcher_get_type	(void) G_GNUC_CONST;
ESourceRegistryWatcher *
		e_source_registry_watcher_new		(ESourceRegistry *registry,
							 const gchar *extension_name);
ESourceRegistry *
		e_source_registry_watcher_get_registry	(ESourceRegistryWatcher *watcher);
const gchar *	e_source_registry_watcher_get_extension_name
							(ESourceRegistryWatcher *watcher);
void		e_source_registry_watcher_reclaim	(ESourceRegistryWatcher *watcher);

G_END_DECLS

#endif /* E_SOURCE_REGISTRY_WATCHER_H */

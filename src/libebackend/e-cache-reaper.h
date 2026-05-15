/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_CACHE_REAPER_H
#define E_CACHE_REAPER_H

#include <glib.h>

/* Standard GObject macros */
#define E_TYPE_CACHE_REAPER \
	(e_cache_reaper_get_type ())
#define E_CACHE_REAPER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CACHE_REAPER, ECacheReaper))
#define E_IS_CACHE_REAPER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CACHE_REAPER))

G_BEGIN_DECLS

typedef struct _ECacheReaper ECacheReaper;
typedef struct _ECacheReaperClass ECacheReaperClass;

void	e_cache_reaper_type_register (GTypeModule *type_module);

GType	e_cache_reaper_get_type			(void);

void	e_cache_reaper_add_private_directory	(ECacheReaper *cache_reaper,
						 const gchar *name);
void	e_cache_reaper_remove_private_directory	(ECacheReaper *cache_reaper,
						 const gchar *name);

G_END_DECLS

#endif /* E_CACHE_REAPER_H */

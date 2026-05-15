/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef EVOLUTION_SOURCE_REGISTRY_METHODS_H
#define EVOLUTION_SOURCE_REGISTRY_METHODS_H

#include <libebackend/libebackend.h>

G_BEGIN_DECLS

gboolean	evolution_source_registry_merge_autoconfig_sources
							(ESourceRegistryServer *server,
							 GError **error);

void		evolution_source_registry_migrate_proxies
							(ESourceRegistryServer *server);

gboolean	evolution_source_registry_migrate_tweak_key_file
							(ESourceRegistryServer *server,
							 GKeyFile *key_file,
							 const gchar *uid);

G_END_DECLS

#endif /* EVOLUTION_SOURCE_REGISTRY_METHODS_H */

/*
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
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

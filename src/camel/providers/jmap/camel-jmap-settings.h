/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_JMAP_SETTINGS_H
#define CAMEL_JMAP_SETTINGS_H

#include <camel/camel.h>

G_BEGIN_DECLS

#define CAMEL_TYPE_JMAP_SETTINGS camel_jmap_settings_get_type ()
G_DECLARE_FINAL_TYPE (CamelJmapSettings, camel_jmap_settings, CAMEL, JMAP_SETTINGS, CamelStoreSettings)

gchar *		camel_jmap_settings_dup_bearer_token	(CamelJmapSettings *settings);
void		camel_jmap_settings_set_bearer_token	(CamelJmapSettings *settings,
							 const gchar *bearer_token);
gboolean	camel_jmap_settings_get_use_bearer_token
							(CamelJmapSettings *settings);
void		camel_jmap_settings_set_use_bearer_token
							(CamelJmapSettings *settings,
							 gboolean use_bearer_token);

G_END_DECLS

#endif /* CAMEL_JMAP_SETTINGS_H */

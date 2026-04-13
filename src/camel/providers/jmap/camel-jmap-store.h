/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_JMAP_STORE_H
#define CAMEL_JMAP_STORE_H

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <camel/camel.h>

G_BEGIN_DECLS

#define CAMEL_TYPE_JMAP_STORE camel_jmap_store_get_type ()
G_DECLARE_FINAL_TYPE (CamelJmapStore, camel_jmap_store, CAMEL, JMAP_STORE, CamelStore)

JsonNode *	camel_jmap_store_call_sync	(CamelJmapStore *store,
						 JsonNode *request,
						 GCancellable *cancellable,
						 GError **error);

const gchar *	camel_jmap_store_get_account_id	(CamelJmapStore *store);

G_END_DECLS

#endif /* CAMEL_JMAP_STORE_H */

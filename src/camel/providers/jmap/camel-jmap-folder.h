/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_JMAP_FOLDER_H
#define CAMEL_JMAP_FOLDER_H

#include <camel/camel.h>

G_BEGIN_DECLS

#define CAMEL_TYPE_JMAP_FOLDER camel_jmap_folder_get_type ()
G_DECLARE_FINAL_TYPE (CamelJmapFolder, camel_jmap_folder, CAMEL, JMAP_FOLDER, CamelFolder)

CamelFolder *	camel_jmap_folder_new		(CamelStore *store,
						 const gchar *folder_name,
						 const gchar *mailbox_id,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_JMAP_FOLDER_H */

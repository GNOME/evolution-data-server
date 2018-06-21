/* e-book-google-utils.h - Google contact conversion utilities.
 *
 * Copyright (C) 2012 Philip Withnall
 *
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
 * Authors: Philip Withnall <philip@tecnocode.co.uk>
 */

#ifndef E_BOOK_GOOGLE_UTILS_H
#define E_BOOK_GOOGLE_UTILS_H

#include <gdata/gdata.h>

#include "e-book-backend-google.h"

#define E_GOOGLE_X_ETAG		"X-EVOLUTION-GOOGLE-ETAG"
#define E_GOOGLE_X_PHOTO_ETAG	"X-EVOLUTION-GOOGLE-PHOTO-ETAG"

G_BEGIN_DECLS

typedef gchar *(*EContactGoogleCreateGroupFunc) (EBookBackendGoogle *bbgoogle,
						 const gchar *category_name,
						 GCancellable *cancellable,
						 GError **error);

GDataEntry *	gdata_entry_new_from_e_contact	(EContact *contact,
						 GHashTable *groups_by_name,
						 GHashTable *system_groups_by_id,
						 EContactGoogleCreateGroupFunc create_group,
						 EBookBackendGoogle *bbgoogle,
						 GCancellable *cancellable) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;
gboolean	gdata_entry_update_from_e_contact
						(GDataEntry *entry,
						 EContact *contact,
						 gboolean ensure_personal_group,
						 GHashTable *groups_by_name,
						 GHashTable *system_groups_by_id,
						 EContactGoogleCreateGroupFunc create_group,
						 EBookBackendGoogle *bbgoogle,
						 GCancellable *cancellable);

EContact *e_contact_new_from_gdata_entry (GDataEntry *entry, GHashTable *groups_by_id,
                                          GHashTable *system_groups_by_entry_id) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;
void e_contact_add_gdata_entry_xml (EContact *contact, GDataEntry *entry);
void e_contact_remove_gdata_entry_xml (EContact *contact);
const gchar *e_contact_get_gdata_entry_xml (EContact *contact, const gchar **edit_uri);

const gchar *e_contact_map_google_with_evo_group (const gchar *group_name, gboolean google_to_evo);

gchar *e_contact_sanitise_google_group_id (const gchar *group_id) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;
gchar *e_contact_sanitise_google_group_name (GDataEntry *group) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

gchar *		e_book_google_utils_time_to_revision	(gint64 unix_time);

G_END_DECLS

#endif /* E_BOOK_GOOGLE_UTILS_H */

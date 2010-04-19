/* util.h - Google contact backend utility functions.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010 Philip Withnall
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <libebook/e-vcard.h>
#include <libebook/e-contact.h>
#include <gdata/gdata-entry.h>

extern gboolean __e_book_backend_google_debug__;

#define __debug__(...) (__e_book_backend_google_debug__ ? g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__) : (void) 0)

GDataEntry *_gdata_entry_new_from_e_contact (EContact *contact);
gboolean _gdata_entry_update_from_e_contact (GDataEntry *entry, EContact *contact);

EContact *_e_contact_new_from_gdata_entry (GDataEntry *entry);
void _e_contact_add_gdata_entry_xml (EContact *contact, GDataEntry *entry);
void _e_contact_remove_gdata_entry_xml (EContact *contact);
const gchar *_e_contact_get_gdata_entry_xml (EContact *contact, const gchar **edit_link);

#endif

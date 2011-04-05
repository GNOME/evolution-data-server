/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Peter Williams <peterw@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef MAIL_TOOLS_H
#define MAIL_TOOLS_H

#include <glib.h>
#include <camel/camel.h>


/* Misc stuff */
#define E_FILTER_SOURCE_INCOMING "incoming" /* performed on incoming email */
#define E_FILTER_SOURCE_DEMAND   "demand"   /* performed on the selected folder
					     * when the user asks for it */
#define E_FILTER_SOURCE_OUTGOING  "outgoing"/* performed on outgoing mail */
#define E_FILTER_SOURCE_JUNKTEST  "junktest"/* check incoming mail for junk */


/* Get the "inbox" for a url (uses global session) */
CamelFolder *mail_tool_get_inbox (const gchar *url, GError **error);

/* Get the "trash" for a url (uses global session) */
CamelFolder *mail_tool_get_trash (const gchar *url, gint connect, GError **error);

/* Does a camel_movemail into the local movemail folder
 * and returns the path to the new movemail folder that was created. which shoudl be freed later */
gchar *mail_tool_do_movemail (const gchar *source_url, GError **error);

struct _camel_header_raw *mail_tool_remove_xevolution_headers (CamelMimeMessage *message);
void mail_tool_restore_xevolution_headers (CamelMimeMessage *message, struct _camel_header_raw *);

/* Generates the subject for a message forwarding @msg */
gchar *mail_tool_generate_forward_subject (CamelMimeMessage *msg);

/* Make a message into an attachment */
CamelMimePart *mail_tool_make_message_attachment (CamelMimeMessage *message);

/* Parse the ui into a real CamelFolder any way we know how. */
CamelFolder *mail_tool_uri_to_folder (const gchar *uri, guint32 flags, GError **error);

GHashTable *mail_lookup_url_table (CamelMimeMessage *mime_message);

CamelFolder *mail_tools_x_evolution_message_parse (gchar *in, guint inlen, GPtrArray **uids);

gchar *mail_tools_folder_to_url (CamelFolder *folder);

gchar *em_uri_to_camel(const gchar *euri);
void em_utils_uids_free (GPtrArray *uids);
gboolean em_utils_folder_is_outbox(CamelFolder *folder, const gchar *uri);
gboolean em_utils_folder_is_sent(CamelFolder *folder, const gchar *uri);
gboolean em_utils_folder_is_drafts(CamelFolder *folder, const gchar *uri);
gboolean em_utils_folder_is_templates (CamelFolder *folder, const gchar *uri);
GHashTable * em_utils_generate_account_hash (void);
EAccount * em_utils_guess_account (CamelMimeMessage *message, CamelFolder *folder);
EAccount * em_utils_guess_account_with_recipients (CamelMimeMessage *message, CamelFolder *folder);


#endif

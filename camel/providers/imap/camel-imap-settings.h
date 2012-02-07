/*
 * camel-imap-settings.h
 *
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
 */

#ifndef CAMEL_IMAP_SETTINGS_H
#define CAMEL_IMAP_SETTINGS_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP_SETTINGS \
	(camel_imap_settings_get_type ())
#define CAMEL_IMAP_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP_SETTINGS, CamelImapSettings))
#define CAMEL_IMAP_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP_SETTINGS, CamelImapSettingsClass))
#define CAMEL_IS_IMAP_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP_SETTINGS))
#define CAMEL_IS_IMAP_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP_SETTINGS))
#define CAMEL_IMAP_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP_SETTINGS))

G_BEGIN_DECLS

typedef struct _CamelImapSettings CamelImapSettings;
typedef struct _CamelImapSettingsClass CamelImapSettingsClass;
typedef struct _CamelImapSettingsPrivate CamelImapSettingsPrivate;

struct _CamelImapSettings {
	CamelOfflineSettings parent;
	CamelImapSettingsPrivate *priv;
};

struct _CamelImapSettingsClass {
	CamelOfflineSettingsClass parent_class;
};

GType		camel_imap_settings_get_type
					(void) G_GNUC_CONST;
gboolean	camel_imap_settings_get_check_all
					(CamelImapSettings *settings);
void		camel_imap_settings_set_check_all
					(CamelImapSettings *settings,
					 gboolean check_all);
gboolean	camel_imap_settings_get_check_subscribed
					(CamelImapSettings *settings);
void		camel_imap_settings_set_check_subscribed
					(CamelImapSettings *settings,
					 gboolean check_subscribed);
CamelFetchHeadersType
		camel_imap_settings_get_fetch_headers
					(CamelImapSettings *settings);
void		camel_imap_settings_set_fetch_headers
					(CamelImapSettings *settings,
					 CamelFetchHeadersType fetch_headers);
const gchar * const *
		camel_imap_settings_get_fetch_headers_extra
					(CamelImapSettings *settings);
gchar **	camel_imap_settings_dup_fetch_headers_extra
					(CamelImapSettings *settings);
void		camel_imap_settings_set_fetch_headers_extra
					(CamelImapSettings *settings,
					 const gchar * const *fetch_headers_extra);
gboolean	camel_imap_settings_get_filter_all
					(CamelImapSettings *settings);
void		camel_imap_settings_set_filter_all
					(CamelImapSettings *settings,
					 gboolean filter_all);
gboolean	camel_imap_settings_get_filter_junk
					(CamelImapSettings *settings);
void		camel_imap_settings_set_filter_junk
					(CamelImapSettings *settings,
					 gboolean filter_junk);
gboolean	camel_imap_settings_get_filter_junk_inbox
					(CamelImapSettings *settings);
void		camel_imap_settings_set_filter_junk_inbox
					(CamelImapSettings *settings,
					 gboolean filter_junk_inbox);
const gchar *	camel_imap_settings_get_namespace
					(CamelImapSettings *settings);
gchar *		camel_imap_settings_dup_namespace
					(CamelImapSettings *settings);
void		camel_imap_settings_set_namespace
					(CamelImapSettings *settings,
					 const gchar *namespace_);
const gchar *	camel_imap_settings_get_real_junk_path
					(CamelImapSettings *settings);
gchar *		camel_imap_settings_dup_real_junk_path
					(CamelImapSettings *settings);
void		camel_imap_settings_set_real_junk_path
					(CamelImapSettings *settings,
					 const gchar *real_junk_path);
const gchar *	camel_imap_settings_get_real_trash_path
					(CamelImapSettings *settings);
gchar *		camel_imap_settings_dup_real_trash_path
					(CamelImapSettings *settings);
void		camel_imap_settings_set_real_trash_path
					(CamelImapSettings *settings,
					 const gchar *real_trash_path);
const gchar *	camel_imap_settings_get_shell_command
					(CamelImapSettings *settings);
gchar *		camel_imap_settings_dup_shell_command
					(CamelImapSettings *settings);
void		camel_imap_settings_set_shell_command
					(CamelImapSettings *settings,
					 const gchar *shell_command);
gboolean	camel_imap_settings_get_use_namespace
					(CamelImapSettings *settings);
void		camel_imap_settings_set_use_namespace
					(CamelImapSettings *settings,
					 gboolean use_namespace);
gboolean	camel_imap_settings_get_use_real_junk_path
					(CamelImapSettings *settings);
void		camel_imap_settings_set_use_real_junk_path
					(CamelImapSettings *settings,
					 gboolean use_real_junk_path);
gboolean	camel_imap_settings_get_use_real_trash_path
					(CamelImapSettings *settings);
void		camel_imap_settings_set_use_real_trash_path
					(CamelImapSettings *settings,
					 gboolean use_real_trash_path);
gboolean	camel_imap_settings_get_use_shell_command
					(CamelImapSettings *settings);
void		camel_imap_settings_set_use_shell_command
					(CamelImapSettings *settings,
					 gboolean use_shell_command);
gboolean	camel_imap_settings_get_use_subscriptions
					(CamelImapSettings *settings);
void		camel_imap_settings_set_use_subscriptions
					(CamelImapSettings *settings,
					 gboolean use_subscriptions);

G_END_DECLS

#endif /* CAMEL_IMAP_SETTINGS_H */

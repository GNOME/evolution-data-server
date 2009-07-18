/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-esource.h"

#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>
#include <libedataserver/e-source-group.h>

#include <stdlib.h>
#include <string.h>

static gboolean is_offline (void);

static ESourceGroup *
find_account_group (ESourceList *source_list, ExchangeAccount *exa)
{
	ESourceGroup *group;
	EAccount *account;

	g_return_val_if_fail (exa != NULL, NULL);
	g_return_val_if_fail (exa->account_name != NULL, NULL);
	g_return_val_if_fail (source_list != NULL, NULL);

	account = exchange_account_fetch (exa);
	g_return_val_if_fail (account != NULL, NULL);
	g_return_val_if_fail (account->uid != NULL, NULL);

	group = e_source_list_peek_group_by_properties (source_list, "account-uid", account->uid, NULL);
	if (!group) {
		/* check whether is stored only with an account name - the old style */
		GSList *g;

		for (g = e_source_list_peek_groups (source_list); g != NULL; g = g->next) {
			group = E_SOURCE_GROUP (g->data);

			if (strcmp (e_source_group_peek_name (group), exa->account_name) == 0)
				break;

			group = NULL;
		}

		if (group)
			e_source_group_set_property (group, "account-uid", account->uid);
	}

	return group;
}

void
add_folder_esource (ExchangeAccount *account,
		    FolderType folder_type,
		    const gchar *folder_name,
		    const gchar *physical_uri)
{
	ESource *source = NULL;
	ESourceGroup *source_group = NULL;
	gchar *relative_uri = NULL;
	GSList *ids;
	GConfClient *client;
	gboolean is_contacts_folder = TRUE, group_new = FALSE, source_new = FALSE;
	const gchar *offline = NULL;
	gchar *username, *windows_domain, *useremail, *authtype = NULL;
	gint mode;
	ESourceList *source_list = NULL;
	gboolean offline_flag, update_selection = TRUE, foriegn_folder;

	client = gconf_client_get_default ();

	/* decode the flag */
	foriegn_folder = folder_type & FORIEGN_FOLDER_FLAG;

	/* Unset the flag */
	folder_type = folder_type & ~FORIEGN_FOLDER_FLAG;

	if (folder_type == EXCHANGE_CONTACTS_FOLDER) {
		source_list = e_source_list_new_for_gconf ( client,
							CONF_KEY_CONTACTS);
		/* Modify the URI handling of Contacts to the same way as calendar and tasks */
		if (!g_str_has_prefix (physical_uri, "gal://")) {
			relative_uri = g_strdup (physical_uri + strlen (EXCHANGE_URI_PREFIX));
		}
	}
	else if (folder_type == EXCHANGE_CALENDAR_FOLDER) {
		source_list = e_source_list_new_for_gconf ( client,
							CONF_KEY_CAL);
		relative_uri = g_strdup (physical_uri + strlen (EXCHANGE_URI_PREFIX));
		is_contacts_folder = FALSE;
	}
	else if (folder_type == EXCHANGE_TASKS_FOLDER) {
		source_list = e_source_list_new_for_gconf ( client,
							CONF_KEY_TASKS);
		relative_uri = g_strdup (physical_uri + strlen (EXCHANGE_URI_PREFIX));
		is_contacts_folder = FALSE;
	}

	exchange_account_is_offline_sync_set (account, &mode);

	windows_domain =  exchange_account_get_windows_domain (account);
	if (windows_domain)
		username = g_strdup_printf ("%s\\%s", windows_domain,
					    exchange_account_get_username (account));
	else
		username = g_strdup (exchange_account_get_username (account));

	useremail = exchange_account_get_email_id (account);
	authtype = exchange_account_get_authtype (account);

        if ((source_group = find_account_group (source_list, account)) == NULL) {
		source_group = e_source_group_new (account->account_name,
						   EXCHANGE_URI_PREFIX);
		if (!e_source_list_add_group (source_list, source_group, -1)) {
			g_object_unref (source_list);
			g_object_unref (source_group);
			g_object_unref (client);
			g_free (relative_uri);
			g_free (username);
			if (authtype)
				g_free (authtype);
			return;
		}
		e_source_group_set_property (source_group, "account-uid", exchange_account_fetch (account)->uid);

		if (is_contacts_folder && g_str_has_prefix (physical_uri, "gal://")) {
			gchar *browse = exchange_account_get_account_uri_param (account, "ad_browse");

			source = e_source_new_with_absolute_uri (folder_name,
								 physical_uri);
			e_source_set_property (source, "completion", "true");
			e_source_set_property (source, "can-browse", browse ? "1" : NULL);
			g_free (browse);
		}
		else {
			source = e_source_new (folder_name, relative_uri);
		}

		if (mode == OFFLINE_MODE) {
			/* If account is marked for offline sync during account
			 * creation, mark all the folders for offline sync
			 */
			e_source_set_property (source, "offline_sync", "1");
		}

		if (foriegn_folder && (folder_type != EXCHANGE_CONTACTS_FOLDER)) {
			e_source_set_property (source, "alarm", "false");
			e_source_set_property (source, "foreign", "1");
			e_source_set_property (source, "subscriber", useremail);
		}

		e_source_set_property (source, "username", username);
		e_source_set_property (source, "auth-domain", "Exchange");
		if (authtype)
			e_source_set_property (source, "auth-type", authtype);
		if (is_contacts_folder)
			e_source_set_property (source, "auth", "plain/password");
		else
			e_source_set_property (source, "auth", "1");
		e_source_group_add_source (source_group, source, -1);
		e_source_list_sync (source_list, NULL);
		group_new = source_new = TRUE;
	}
	else {
                /* source group already exists*/
		if ((source = e_source_group_peek_source_by_name (source_group,
							folder_name)) == NULL) {
			if (is_contacts_folder && g_str_has_prefix (physical_uri, "gal://")) {
				gchar *browse = exchange_account_get_account_uri_param (account, "ad_browse");

				source = e_source_new_with_absolute_uri (
						folder_name, physical_uri);
				e_source_set_property (source, "completion", "true");
				e_source_set_property (source, "can-browse", browse ? "1" : NULL);
				g_free (browse);
			}
			else {
				source = e_source_new (folder_name, relative_uri);
			}

			if (mode == OFFLINE_MODE)
				e_source_set_property (source, "offline_sync", "1");

			e_source_set_property (source, "username", username);
			e_source_set_property (source, "auth-domain", "Exchange");
			if (authtype)
				e_source_set_property (source, "auth-type", authtype);
			if (is_contacts_folder)
				e_source_set_property (source, "auth", "plain/password");
			else
				e_source_set_property (source, "auth", "1");

			if (foriegn_folder && (folder_type != EXCHANGE_CONTACTS_FOLDER)) {
				e_source_set_property (source, "alarm", "false");
				e_source_set_property (source, "foreign", "1");
				e_source_set_property (source, "subscriber", useremail);
			}

			e_source_group_add_source (source_group, source, -1);
			source_new = TRUE;
			e_source_list_sync (source_list, NULL);
		} else {
			update_selection = FALSE;
			/* source group and source both already exist */
			offline = e_source_get_property (source, "offline_sync");
			if (!offline) {
				/* Folder doesn't have any offline property set */
				if (mode == OFFLINE_MODE) {
					e_source_set_property (source, "offline_sync", "1");
					e_source_list_sync (source_list, NULL);
				}
			}

			if (is_contacts_folder && g_str_has_prefix (physical_uri, "gal://")) {
				gchar *browse = exchange_account_get_account_uri_param (account, "ad_browse");
				const gchar *old_browse = e_source_get_property (source, "can-browse");

				if ((old_browse || browse) && (!old_browse || !browse)) {
					e_source_set_property (source, "can-browse", browse ? "1" : NULL);
				}

				g_free (browse);
			}
		}
	}

	offline_flag = is_offline ();
	if (source && !is_contacts_folder && update_selection) {

		/* Select the folder created */
		if (folder_type == EXCHANGE_CALENDAR_FOLDER && !offline_flag) {
			ids = gconf_client_get_list (client,
					     CONF_KEY_SELECTED_CAL_SOURCES,
					     GCONF_VALUE_STRING, NULL);
			ids = g_slist_append (ids,
					g_strdup (e_source_peek_uid (source)));
			gconf_client_set_list (client,
				       CONF_KEY_SELECTED_CAL_SOURCES,
				       GCONF_VALUE_STRING, ids, NULL);
			g_slist_foreach (ids, (GFunc) g_free, NULL);
			g_slist_free (ids);
		}
		else if (folder_type == EXCHANGE_TASKS_FOLDER && !offline_flag) {
			ids = gconf_client_get_list (client,
					     CONF_KEY_SELECTED_TASKS_SOURCES,
					     GCONF_VALUE_STRING, NULL);

			ids = g_slist_append (ids,
					g_strdup (e_source_peek_uid (source)));
			gconf_client_set_list (client,
				       CONF_KEY_SELECTED_TASKS_SOURCES,
				       GCONF_VALUE_STRING, ids, NULL);
			g_slist_foreach (ids, (GFunc) g_free, NULL);
			g_slist_free (ids);
		}
	}

	g_free (relative_uri);
	g_free (username);
	if (authtype)
		g_free (authtype);

	if (source_new)
		g_object_unref (source);
	if (group_new)
		g_object_unref (source_group);
	g_object_unref (source_list);
	g_object_unref (client);
}

void
remove_folder_esource (ExchangeAccount *account,
		       FolderType folder_type,
		       const gchar *physical_uri)
{
	ESourceGroup *group;
	ESource *source;
	GSList *groups;
	GSList *sources;
	gboolean found_group, is_contacts_folder = TRUE;
	gchar *read_uri = NULL;
	const gchar *source_uid;
	GSList *ids, *temp_ids, *node_to_be_deleted;
	GConfClient *client;
	ESourceList *source_list = NULL;

	client = gconf_client_get_default ();

	/* Remove ESource for a given folder */
	if (folder_type == EXCHANGE_CONTACTS_FOLDER) {
		source_list = e_source_list_new_for_gconf ( client,
							CONF_KEY_CONTACTS);
	}
	else if (folder_type == EXCHANGE_CALENDAR_FOLDER) {
		source_list = e_source_list_new_for_gconf ( client,
							CONF_KEY_CAL);
		is_contacts_folder = FALSE;
	}
	else if (folder_type == EXCHANGE_TASKS_FOLDER) {
		source_list = e_source_list_new_for_gconf ( client,
							CONF_KEY_TASKS);
		is_contacts_folder = FALSE;
	}

	groups = e_source_list_peek_groups (source_list);
	found_group = FALSE;

	for (; groups != NULL && !found_group; groups = g_slist_next (groups)) {
		group = E_SOURCE_GROUP (groups->data);

		if (strcmp (e_source_group_peek_name (group), account->account_name) == 0
                    &&
                   strcmp (e_source_group_peek_base_uri (group), EXCHANGE_URI_PREFIX) == 0) {

			sources = e_source_group_peek_sources (group);

			for (; sources != NULL; sources = g_slist_next (sources)) {

				source = E_SOURCE (sources->data);
				read_uri = e_source_get_uri (source);

				if (strcmp (read_uri, physical_uri) == 0) {

					source_uid = e_source_peek_uid (source);
					/* Folder Deleted - Remove only the source */
					/*
					e_source_group_remove_source_by_uid (
								group,
								source_uid);
					*/
					e_source_group_remove_source (
								group,
								source);
					e_source_list_sync (source_list, NULL);
					if (!is_contacts_folder) {
						/* Remove from the selected folders */
						if (folder_type == EXCHANGE_CALENDAR_FOLDER) {
							ids = gconf_client_get_list (
									client,
									CONF_KEY_SELECTED_CAL_SOURCES,
									GCONF_VALUE_STRING, NULL);
							if (ids) {
								node_to_be_deleted = g_slist_find_custom (ids,
											source_uid,
											(GCompareFunc) strcmp);
								if (node_to_be_deleted) {
									g_free (node_to_be_deleted->data);
									ids = g_slist_delete_link (ids,
											node_to_be_deleted);
								}
							}
							temp_ids  = ids;
							for (; temp_ids != NULL; temp_ids = g_slist_next (temp_ids))
							g_free (temp_ids->data);
							g_slist_free (ids);
						}
						else if (folder_type == EXCHANGE_TASKS_FOLDER) {
							ids = gconf_client_get_list (client,
									CONF_KEY_SELECTED_TASKS_SOURCES,
									GCONF_VALUE_STRING, NULL);
							if (ids) {
								node_to_be_deleted = g_slist_find_custom (ids,
											source_uid,
											(GCompareFunc) strcmp);
								if (node_to_be_deleted) {
									g_free (node_to_be_deleted->data);
									ids = g_slist_delete_link (ids,
											node_to_be_deleted);
								}
							}
							temp_ids  = ids;
							for (; temp_ids != NULL; temp_ids = g_slist_next (temp_ids))
								g_free (temp_ids->data);
							g_slist_free (ids);
						}
					}
                                        found_group = TRUE;
                                        break;
                                }
				g_free (read_uri);
                        }
                }
        }
	g_object_unref (source_list);
	g_object_unref (client);
}

static gboolean
is_offline (void)
{
	GConfClient *client;
	GConfValue *value;
	gboolean offline = FALSE;

	client = gconf_client_get_default ();
	value = gconf_client_get (client,
					"/apps/evolution/shell/start_offline", NULL);
	if (value)
		offline = gconf_value_get_bool (value);

	g_object_unref (client);
	gconf_value_free (value);
	return offline;
}

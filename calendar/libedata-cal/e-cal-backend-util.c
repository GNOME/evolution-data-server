/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>
#include "e-cal-backend-util.h"
#include "libedataserver/e-account-list.h"

static EAccountList *accounts;

/**
 * e_cal_backend_mail_account_get_default:
 * @address: Placeholder for default address.
 * @name: Placeholder for name.
 *
 * Retrieve the default mail account as stored in Evolution configuration.
 *
 * Returns: TRUE if there is a default account, FALSE otherwise.
 */
gboolean
e_cal_backend_mail_account_get_default (gchar **address, gchar **name)
{
	const EAccount *account;

	if (accounts == NULL) {
		GConfClient *gconf = gconf_client_get_default ();

		accounts = e_account_list_new (gconf);

		g_object_unref (gconf);
	}

	account = e_account_list_get_default(accounts);
	if (account) {
		*address = g_strdup(account->id->address);
		*name = g_strdup(account->id->name);
	}

	return account != NULL;
}

/**
 * e_cal_backend_mail_account_is_valid:
 * @user: User name for the account to check.
 * @name: Placeholder for the account name.
 *
 * Checks that a mail account is valid, and returns its name.
 *
 * Returns: TRUE if the account is valid, FALSE if not.
 */
gboolean
e_cal_backend_mail_account_is_valid (gchar *user, gchar **name)
{
	const EAccount *account;

	if (accounts == NULL) {
		GConfClient *gconf = gconf_client_get_default ();

		accounts = e_account_list_new (gconf);

		g_object_unref (gconf);
	}

	account = e_account_list_find(accounts, E_ACCOUNT_FIND_ID_ADDRESS, user);
	if (account)
		*name = g_strdup(account->id->name);

	return account != NULL;
}

/**
 * e_cal_backend_status_to_string:
 *
 * Converts status code to string.
 *
 * Since: 2.24
 **/
const gchar *
e_cal_backend_status_to_string (GNOME_Evolution_Calendar_CallStatus status)
{
	switch (status) {
	case GNOME_Evolution_Calendar_Success:
		return _("No error");
	case GNOME_Evolution_Calendar_RepositoryOffline:
		return _("Repository is offline");
	case GNOME_Evolution_Calendar_PermissionDenied:
		return _("Permission denied");
	case GNOME_Evolution_Calendar_InvalidRange:
		return _("Invalid range");
	case GNOME_Evolution_Calendar_ObjectNotFound:
		return _("Object not found");
	case GNOME_Evolution_Calendar_InvalidObject:
		return _("Invalid object");
	case GNOME_Evolution_Calendar_ObjectIdAlreadyExists:
		return _("Object ID already exists");
	case GNOME_Evolution_Calendar_AuthenticationFailed:
		return _("Authentication failed");
	case GNOME_Evolution_Calendar_AuthenticationRequired:
		return _("Authentication required");
	case GNOME_Evolution_Calendar_UnsupportedField:
		return _("Unsupported field");
	case GNOME_Evolution_Calendar_UnsupportedMethod:
		return _("Unsupported method");
	case GNOME_Evolution_Calendar_UnsupportedAuthenticationMethod:
		return _("Unsupported authentication method");
	case GNOME_Evolution_Calendar_TLSNotAvailable:
		return _("TLS not available");
	case GNOME_Evolution_Calendar_NoSuchCal:
		return _("No such calendar");
	case GNOME_Evolution_Calendar_UnknownUser:
		return _("Unknown User");
	case GNOME_Evolution_Calendar_OfflineUnavailable:
		return _("Offline mode unavailable");
	case GNOME_Evolution_Calendar_SearchSizeLimitExceeded:
		return _("Search size limit exceeded");
	case GNOME_Evolution_Calendar_SearchTimeLimitExceeded:
		return _("Search time limit exceeded");
	case GNOME_Evolution_Calendar_InvalidQuery:
		return _("Invalid query");
	case GNOME_Evolution_Calendar_QueryRefused:
		return _("Query refused");
	case GNOME_Evolution_Calendar_CouldNotCancel:
		return _("Could not cancel operation");
	default:
	case GNOME_Evolution_Calendar_OtherError:
		return _("Unknown error");
	case GNOME_Evolution_Calendar_InvalidServerVersion:
		return _("Invalid server version");
	}

	return NULL;
}

/**
 * is_attendee_declined:
 * @icalcomp: Component where to check the attendee list.
 * @email: Attendee's email to look for.
 *
 * Returns: Whether the required attendee declined or not.
 *          It's not necessary to have this attendee in the list.
 **/
static gboolean
is_attendee_declined (icalcomponent *icalcomp, const gchar *email)
{
	icalproperty *prop;
	icalparameter *param;

	g_return_val_if_fail (icalcomp != NULL, FALSE);
	g_return_val_if_fail (email != NULL, FALSE);

	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY)) {
		gchar *attendee;
		gchar *text = NULL;

		attendee = icalproperty_get_value_as_string_r (prop);
		if (!attendee)
			continue;

		if (!g_ascii_strncasecmp (attendee, "mailto:", 7))
			text = g_strdup (attendee + 7);
		text = g_strstrip (text);

		if (!g_ascii_strcasecmp (email, text)) {
			g_free (text);
			g_free (attendee);
			break;
		}
		g_free (text);
		g_free (attendee);
	}

	if (!prop)
		return FALSE;

	param = icalproperty_get_first_parameter (prop, ICAL_PARTSTAT_PARAMETER);

	return param && icalparameter_get_partstat (param) == ICAL_PARTSTAT_DECLINED;
}

/**
 * e_cal_backend_user_declined:
 * @icalcomp: Component where to check.
 *
 * Returns: Whether icalcomp contains attendee with a mail same as any of
 *          configured enabled mail account and whether this user declined.
 *
 * Since: 2.26
 **/
gboolean
e_cal_backend_user_declined (icalcomponent *icalcomp)
{
	gboolean res = FALSE;
	EAccountList *accounts;
	GConfClient *gconf;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	gconf = gconf_client_get_default ();
	accounts = e_account_list_new (gconf);

	if (accounts) {
		EIterator *it;

		for (it = e_list_get_iterator (E_LIST (accounts)); e_iterator_is_valid (it); e_iterator_next (it)) {
			EAccount *account = (EAccount *) e_iterator_get (it);

			if (account && account->enabled && e_account_get_string (account, E_ACCOUNT_ID_ADDRESS)) {
				res = is_attendee_declined (icalcomp, e_account_get_string (account, E_ACCOUNT_ID_ADDRESS));

				if (res)
					break;
			}
		}

		g_object_unref (it);
		g_object_unref (accounts);
	}

	g_object_unref (gconf);

	return res;
}


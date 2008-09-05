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
 * Return value: TRUE if there is a default account, FALSE otherwise.
 */
gboolean
e_cal_backend_mail_account_get_default (char **address, char **name)
{
	const EAccount *account;

	/* FIXME I think this leaks the gconf client */
	if (accounts == NULL)
		accounts = e_account_list_new(gconf_client_get_default());

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
 * Return value: TRUE if the account is valid, FALSE if not.
 */
gboolean
e_cal_backend_mail_account_is_valid (char *user, char **name)
{
	const EAccount *account;

	/* FIXME I think this leaks the gconf client */
	if (accounts == NULL)
		accounts = e_account_list_new(gconf_client_get_default());

	account = e_account_list_find(accounts, E_ACCOUNT_FIND_ID_ADDRESS, user);
	if (account)
		*name = g_strdup(account->id->name);

	return account != NULL;
}

/**
 * e_cal_backend_status_to_string:
 *
 * Converts status code to string.
 **/
const char *
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
	/* Translators: This means "Offline mode unavailable" */
		return _("Offline unavailable");
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

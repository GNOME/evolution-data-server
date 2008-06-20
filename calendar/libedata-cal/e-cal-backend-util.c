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

#include <string.h>
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

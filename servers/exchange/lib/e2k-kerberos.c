/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e2k-kerberos.h"
#include <krb5.h>

static krb5_context
e2k_kerberos_context_new (const char *domain)
{
	krb5_context ctx;
	char *realm;

	if (krb5_init_context (&ctx) != 0)
		return NULL;

	realm = g_ascii_strup (domain, strlen (domain));
	krb5_set_default_realm (ctx, realm);
	g_free (realm);

	return ctx;
}

static E2kKerberosResult
krb5_result_to_e2k_kerberos_result (int result)
{
	switch (result) {
	case 0:
		return E2K_KERBEROS_OK;

	case KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN:
		return E2K_KERBEROS_USER_UNKNOWN;

	case KRB5KRB_AP_ERR_BAD_INTEGRITY:
	case KRB5KDC_ERR_PREAUTH_FAILED:
		return E2K_KERBEROS_PASSWORD_INCORRECT;

	case KRB5KDC_ERR_KEY_EXP:
		return E2K_KERBEROS_PASSWORD_EXPIRED;

	case KRB5_KDC_UNREACH:
		return E2K_KERBEROS_KDC_UNREACHABLE;

	case KRB5KRB_AP_ERR_SKEW:
		return E2K_KERBEROS_TIME_SKEW;

	default:
		g_warning ("Unexpected kerberos error %d", result);
		return E2K_KERBEROS_FAILED;
	}
}

static E2kKerberosResult
get_init_cred (krb5_context ctx, const char *usr_name, const char *passwd,
	       const char *in_tkt_service, krb5_creds *cred)
{
	krb5_principal principal;
	krb5_get_init_creds_opt opt;
	krb5_error_code result;

	result = krb5_parse_name (ctx, usr_name, &principal);
	if (result)
		return E2K_KERBEROS_USER_UNKNOWN;

	krb5_get_init_creds_opt_init (&opt);
	krb5_get_init_creds_opt_set_tkt_life (&opt, 5*60);
	krb5_get_init_creds_opt_set_renew_life (&opt, 0);
	krb5_get_init_creds_opt_set_forwardable (&opt, 0);
	krb5_get_init_creds_opt_set_proxiable (&opt, 0);

	result = krb5_get_init_creds_password (ctx, cred, principal, passwd,
					       NULL, NULL, 0,
					       in_tkt_service, &opt);
	krb5_free_principal (ctx, principal);

	return krb5_result_to_e2k_kerberos_result (result);
}

/**
 * e2k_kerberos_change_password
 * @user: username
 * @domain: Windows (2000) domain name
 * @old_password: currrent password
 * @new_password: password to be changed to
 *
 * Changes the password for the given user
 *
 * Return value: an #E2kKerberosResult
 **/
E2kKerberosResult
e2k_kerberos_change_password (const char *user, const char *domain,
			      const char *old_password, const char *new_password)
{
	krb5_context ctx;
	krb5_creds creds;
	krb5_data res_code_string, res_string;
	E2kKerberosResult result;
	int res_code;

	ctx = e2k_kerberos_context_new (domain);
	if (!ctx)
		return E2K_KERBEROS_FAILED;

	result = get_init_cred (ctx, user, old_password,
				"kadmin/changepw", &creds);
	if (result != E2K_KERBEROS_OK) {
		krb5_free_context (ctx);
		return result;
	}

	result = krb5_change_password (ctx, &creds, (char *)new_password,
				       &res_code, &res_code_string, &res_string);
	krb5_free_cred_contents (ctx, &creds);
	krb5_free_data_contents (ctx, &res_code_string);
	krb5_free_data_contents (ctx, &res_string);
	krb5_free_context (ctx);

	if (result != 0)
		return krb5_result_to_e2k_kerberos_result (result);
	else if (res_code != 0)
		return E2K_KERBEROS_FAILED;
	else
		return E2K_KERBEROS_OK;
}

/**
 * e2k_kerberos_check_password:
 * @user: username
 * @domain: Windows (2000) domain name
 * @password: current password
 *
 * Checks if the password is valid, invalid, or expired
 *
 * Return value: %E2K_KERBEROS_OK, %E2K_KERBEROS_USER_UNKNOWN,
 * %E2K_KERBEROS_PASSWORD_INCORRECT, %E2K_KERBEROS_PASSWORD_EXPIRED,
 * or %E2K_KERBEROS_FAILED (for unknown errors)
 **/
E2kKerberosResult
e2k_kerberos_check_password (const char *user, const char *domain,
			     const char *password)
{
	krb5_context ctx;
	krb5_creds creds;
	E2kKerberosResult result;

	ctx = e2k_kerberos_context_new (domain);
	if (!ctx)
		return E2K_KERBEROS_FAILED;

	result = get_init_cred (ctx, user, password, NULL, &creds);

	krb5_free_context (ctx);
	if (result == E2K_KERBEROS_OK)
		krb5_free_cred_contents (ctx, &creds);

	return result;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

/* exchange-oof: Out of Office code */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-oof.h"
#include "exchange-account.h"
#include "e2k-propnames.h"
#include "e2k-utils.h"
#include "e2k-uri.h"

#include <string.h>

/* Taken from gal/util/e-util.c */
static char *
find_str_case (const char *haystack, const char *needle)
{
	/* find the needle in the haystack neglecting case */
	const char *ptr;
	int len;

	g_return_val_if_fail (haystack != NULL, NULL);
	g_return_val_if_fail (needle != NULL, NULL);

	len = strlen(needle);
	if (len > strlen(haystack))
		return NULL;

	if (len == 0)
		return (char *) haystack;

	for (ptr = haystack; *(ptr + len - 1) != '\0'; ptr++)
		if (!g_strncasecmp (ptr, needle, len))
			return (char *) ptr;

	return NULL;

}
/**
 * exchange_oof_get:
 * @account: an #ExchangeAccount
 * @oof: pointer to variable to pass back OOF state in
 * @message: pointer to variable to pass back OOF message in
 *
 * Checks if Out-of-Office is enabled for @account and returns the
 * state in *@oof and the message in *@message (which the caller
 * must free).
 *
 * Return value: %TRUE if the OOF state was read, %FALSE if an error
 * occurred.
 **/
gboolean
exchange_oof_get (ExchangeAccount *account, gboolean *oof, char **message)
{
	E2kContext *ctx;
	E2kHTTPStatus status;
	char *url, *body, *p, *checked, *ta_start, *ta_end;
	int len;

	ctx = exchange_account_get_context (account);
	if (!ctx)
		return FALSE;

	if (!message) {
		/* Do this the easy way */
		const char *prop = E2K_PR_EXCHANGE_OOF_STATE;
		E2kResult *results;
		int nresults;

		url = e2k_uri_concat (account->home_uri, "NON_IPM_SUBTREE/");
		status = e2k_context_propfind (ctx, NULL, url, &prop, 1,
					       &results, &nresults);
		g_free (url);
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status) || nresults == 0)
			return FALSE;

		prop = e2k_properties_get_prop (results[0].props, E2K_PR_EXCHANGE_OOF_STATE);
		*oof = prop && atoi (prop);

		e2k_results_free (results, nresults);
		return TRUE;
	}

	url = e2k_uri_concat (account->home_uri, "?Cmd=options");
	status = e2k_context_get_owa (ctx, NULL, url, FALSE, &body, &len);
	g_free (url);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return FALSE;

	p = find_str_case (body, "<!--End OOF Assist-->");
	if (p)
		*p = '\0';
	else
		body[len - 1] = '\0';

	p = find_str_case (body, "name=\"OofState\"");
	if (p)
		p = find_str_case (body, "value=\"1\"");
	if (!p) {
		g_warning ("Could not find OofState in options page");
		g_free (body);
		return FALSE;
	}

	checked = find_str_case (p, "checked");
	*oof = (checked && checked < strchr (p, '>'));

	if (message) {
		ta_end = find_str_case (p, "</textarea>");
		if (!ta_end) {
			g_warning ("Could not find OOF text in options page");
			g_free (body);
			*message = g_strdup ("");
			return TRUE;
		}
		for (ta_start = ta_end - 1; ta_start > p; ta_start--) {
			if (*ta_start == '>')
				break;
		}
		if (*ta_start++ != '>') {
			g_warning ("Could not find OOF text in options page");
			g_free (body);
			*message = g_strdup ("");
			return TRUE;
		}

		*message = g_strndup (ta_start, ta_end - ta_start);
		/* FIXME: HTML decode */
		
	}

	return TRUE;
}

/**
 * exchange_oof_set:
 * @account: an #ExchangeAccount
 * @oof: new OOF state
 * @message: new OOF message, or %NULL
 *
 * Sets the OOF state for @account to @oof.
 *
 * Return value: %TRUE if the OOF state was updated, %FALSE if an
 * error occurred.
 **/
gboolean
exchange_oof_set (ExchangeAccount *account, gboolean oof, const char *message)
{
	E2kContext *ctx;
	E2kHTTPStatus status;

	ctx = exchange_account_get_context (account);
	if (!ctx)
		return FALSE;

	if (message) {
		char *body, *message_enc;

		message_enc = e2k_uri_encode (message, FALSE, NULL);
		body = g_strdup_printf ("Cmd=options&OofState=%d&"
					"OofReply=%s",
					oof ? 1 : 0, message_enc);
		status = e2k_context_post (ctx, NULL, account->home_uri,
					   "application/x-www-form-urlencoded",
					   body, strlen (body), NULL, NULL);
		g_free (message_enc);
		g_free (body);
	} else {
		E2kProperties *props;
		char *url;

		props = e2k_properties_new ();
		e2k_properties_set_bool (props, E2K_PR_EXCHANGE_OOF_STATE, oof);
		url = e2k_uri_concat (account->home_uri, "NON_IPM_SUBTREE/");
		/* Need to pass TRUE for "create" here or it won't work */
		status = e2k_context_proppatch (ctx, NULL, url, props,
						TRUE, NULL);
		g_free (url);
		e2k_properties_free (props);
	}

	return E2K_HTTP_STATUS_IS_SUCCESSFUL (status) ||
		E2K_HTTP_STATUS_IS_REDIRECTION (status);
}

/*
 * e-credentials.c
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
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "e-data-server-util.h"

#include "e-credentials.h"

struct _ECredentialsPrivate
{
	GHashTable *keys;
	GHashTable *peek_keys;
};

static gboolean
key_equal (gconstpointer str1, gconstpointer str2)
{
	g_return_val_if_fail (str1 != NULL, FALSE);
	g_return_val_if_fail (str2 != NULL, FALSE);

	if (str1 == str2)
		return TRUE;

	return g_ascii_strcasecmp (str1, str2) == 0;
}

ECredentials *
e_credentials_new (void)
{
	ECredentials *credentials;

	credentials = g_new0 (ECredentials, 1);
	credentials->priv = g_new0 (ECredentialsPrivate, 1);
	credentials->priv->keys = g_hash_table_new_full (g_str_hash, key_equal, g_free, (GDestroyNotify) e_credentials_util_safe_free_string);
	credentials->priv->peek_keys = g_hash_table_new_full (g_str_hash, key_equal, g_free, (GDestroyNotify) e_credentials_util_safe_free_string);

	return credentials;
}

/* Expects @keys as NULL terminate list of strings "key:encoded_value".
   The same can be returned from e_credentials_to_strv().
*/
ECredentials *
e_credentials_new_strv (const gchar * const *keys)
{
	ECredentials *credentials;
	gint ii;

	g_return_val_if_fail (keys != NULL, NULL);

	credentials = e_credentials_new ();

	for (ii = 0; keys[ii]; ii++) {
		const gchar *key = keys[ii], *sep;

		sep = strchr (key, ':');

		/* skip empty and invalid values */
		if (sep)
			g_hash_table_insert (credentials->priv->keys, g_strndup (key, sep - key), g_strdup (sep + 1));
	}

	return credentials;
}

/* NULL-terminated list of string pairs <key, value>; value is in a clear form */
ECredentials *
e_credentials_new_args	(const gchar *key, ...)
{
	ECredentials *credentials;
	va_list va;

	g_return_val_if_fail (key != NULL, NULL);

	credentials = e_credentials_new ();

	va_start (va, key);

	while (key) {
		const gchar *value = va_arg (va, const gchar *);

		if (key && *key && value && *value)
			e_credentials_set (credentials, key, value);

		key = va_arg (va, const gchar *);
	}

	va_end (va);

	return credentials;
}

static void
copy_keys_cb (gpointer key, gpointer value, gpointer hash_table)
{
	g_hash_table_insert (hash_table, g_strdup (key), g_strdup (value));
}

ECredentials *
e_credentials_new_clone	(const ECredentials *credentials)
{
	ECredentials *res;

	g_return_val_if_fail (credentials != NULL, NULL);
	g_return_val_if_fail (credentials->priv != NULL, NULL);
	g_return_val_if_fail (credentials->priv->keys != NULL, NULL);

	res = e_credentials_new ();

	g_hash_table_foreach (credentials->priv->keys, copy_keys_cb, res->priv->keys);

	return res;
}

void
e_credentials_free (ECredentials *credentials)
{
	if (!credentials)
		return;

	g_return_if_fail (credentials->priv != NULL);

	g_hash_table_destroy (credentials->priv->keys);
	g_hash_table_destroy (credentials->priv->peek_keys);
	g_free (credentials->priv);
	g_free (credentials);
}

static void
add_to_array_cb (gpointer key, gpointer value, gpointer ptr_array)
{
	if (key && value && ptr_array) {
		gchar *str = g_strconcat (key, ":", value, NULL);

		g_ptr_array_add (ptr_array, e_util_utf8_make_valid (str));

		g_free (str);
	}
}

/* Returns NULL-terminated array of strings with keys and encoded values;
   To read them back pass this pointer to e_credentials_new(). As it returns
   newly allocated string then this should be freed with g_strfreev()
   when no longer needed.
*/
gchar **
e_credentials_to_strv (const ECredentials *credentials)
{
	GPtrArray *array;

	g_return_val_if_fail (credentials != NULL, NULL);
	g_return_val_if_fail (credentials->priv != NULL, NULL);
	g_return_val_if_fail (credentials->priv->keys != NULL, NULL);

	array = g_ptr_array_sized_new (g_hash_table_size (credentials->priv->keys) + 1);

	g_hash_table_foreach (credentials->priv->keys, add_to_array_cb, array);

	/* NULL-terminated */
	g_ptr_array_add (array, NULL);

	return (gchar **) g_ptr_array_free (array, FALSE);
}

static gchar *
encode_string (const gchar *decoded)
{
	gsize len, ii;
	guchar xval, *copy;
	gchar *res;

	if (!decoded || !*decoded)
		return NULL;

	copy = (guchar *) g_strdup (decoded);
	len = strlen ((const gchar *) copy);

	xval = 17;
	for (ii = 0; ii < len; ii++) {
		copy[ii] = copy[ii] ^ xval;
		xval += 17;
	}

	res = g_base64_encode (copy, len);

	g_free (copy);

	return res;
}

static gchar *
decode_string (const gchar *encoded)
{
	guchar *data, xval;
	gsize len = 0, ii;
	gchar *res;

	g_return_val_if_fail (encoded != NULL, NULL);
	g_return_val_if_fail (*encoded, NULL);

	data = g_base64_decode (encoded, &len);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (len > 0, NULL);

	xval = 17;
	for (ii = 0; ii < len; ii++) {
		data[ii] = data[ii] ^ xval;
		xval += 17;
	}

	res = g_strndup ((const gchar *) data, len);

	e_credentials_util_safe_free_string ((gchar *) data);

	return res;
}

/* sets value for a key, if value is NULL or an empty string then the key is removed.
   the value is supposed to be in a clear form (unencoded).
   'key' cannot contain colon.
*/
void
e_credentials_set (ECredentials *credentials, const gchar *key, const gchar *value)
{
	g_return_if_fail (credentials != NULL);
	g_return_if_fail (credentials->priv != NULL);
	g_return_if_fail (credentials->priv->keys != NULL);
	g_return_if_fail (credentials->priv->peek_keys != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (*key);
	g_return_if_fail (strchr (key, ':') == NULL);

	g_hash_table_remove (credentials->priv->peek_keys, key);

	if (!value) {
		g_hash_table_remove (credentials->priv->keys, key);
	} else {
		g_hash_table_insert (credentials->priv->keys, g_strdup (key), encode_string (value));
	}
}

/* Returned pointer should be freed with e_credentials_util_safe_free_string()
   when no longer needed.
*/
gchar *
e_credentials_get (const ECredentials *credentials, const gchar *key)
{
	const gchar *stored;

	g_return_val_if_fail (credentials != NULL, NULL);
	g_return_val_if_fail (credentials->priv != NULL, NULL);
	g_return_val_if_fail (credentials->priv->keys != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (*key, NULL);

	stored = g_hash_table_lookup (credentials->priv->keys, key);
	if (!stored)
		return NULL;

	return decode_string (stored);
}

/* peeks value for a key, in a clear form. The value is valid until free
   of the @credentials structure or until the key value is rewritten
   by e_credentials_set()
*/
const gchar *
e_credentials_peek (ECredentials *credentials, const gchar *key)
{
	gchar *value;

	g_return_val_if_fail (credentials != NULL, NULL);
	g_return_val_if_fail (credentials->priv != NULL, NULL);
	g_return_val_if_fail (credentials->priv->peek_keys != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (*key, NULL);

	value = g_hash_table_lookup (credentials->priv->peek_keys, key);
	if (value)
		return value;

	value = e_credentials_get (credentials, key);
	if (value)
		g_hash_table_insert (credentials->priv->peek_keys, g_strdup (key), value);

	return value;
}

struct equal_data
{
	gboolean equal;
	GHashTable *keys;
};

static void
check_equal_cb (gpointer key, gpointer value, gpointer user_data)
{
	struct equal_data *ed = user_data;

	g_return_if_fail (ed != NULL);
	g_return_if_fail (ed->keys != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	ed->equal = ed->equal && g_strcmp0 (value, g_hash_table_lookup (ed->keys, key)) == 0;
}

/* Returns whether two credential structurs contain the same keys with same values */
gboolean
e_credentials_equal (const ECredentials *credentials1, const ECredentials *credentials2)
{
	struct equal_data ed;

	if (!credentials1 && !credentials2)
		return TRUE;

	if (credentials1 == credentials2)
		return TRUE;

	if (!credentials1 || !credentials2)
		return FALSE;

	g_return_val_if_fail (credentials1->priv != NULL, FALSE);
	g_return_val_if_fail (credentials1->priv->keys != NULL, FALSE);
	g_return_val_if_fail (credentials2->priv != NULL, FALSE);
	g_return_val_if_fail (credentials2->priv->keys != NULL, FALSE);

	if (g_hash_table_size (credentials1->priv->keys) != g_hash_table_size (credentials2->priv->keys))
		return FALSE;

	ed.equal = TRUE;
	ed.keys = credentials2->priv->keys;

	g_hash_table_foreach (credentials1->priv->keys, check_equal_cb, &ed);

	return ed.equal;
}

/* Returns whether two credentials structures has same keys. Key names are NULL-terminated. */
gboolean
e_credentials_equal_keys (const ECredentials *credentials1, const ECredentials *credentials2, const gchar *key1, ...)
{
	va_list va;
	gboolean equal = TRUE;

	g_return_val_if_fail (credentials1 != NULL, FALSE);
	g_return_val_if_fail (credentials1->priv != NULL, FALSE);
	g_return_val_if_fail (credentials1->priv->keys != NULL, FALSE);
	g_return_val_if_fail (credentials2 != NULL, FALSE);
	g_return_val_if_fail (credentials2->priv != NULL, FALSE);
	g_return_val_if_fail (credentials2->priv->keys != NULL, FALSE);
	g_return_val_if_fail (key1 != NULL, FALSE);

	va_start (va, key1);

	while (key1 && equal) {
		equal = g_strcmp0 (g_hash_table_lookup (credentials1->priv->keys, key1), g_hash_table_lookup (credentials2->priv->keys, key1)) == 0;

		key1 = va_arg (va, const gchar *);
	}

	va_end (va);

	return equal;
}	

/**
 * Returns whether @credentials contains @key.
 * This key is non-NULL and non-empty string.
 **/
gboolean
e_credentials_has_key (const ECredentials *credentials, const gchar *key)
{
	g_return_val_if_fail (credentials != NULL, FALSE);
	g_return_val_if_fail (credentials->priv != NULL, FALSE);
	g_return_val_if_fail (credentials->priv->keys != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (*key, FALSE);

	return g_hash_table_lookup (credentials->priv->keys, key) != NULL;
}

guint
e_credentials_keys_size (const ECredentials *credentials)
{
	g_return_val_if_fail (credentials != NULL, 0);
	g_return_val_if_fail (credentials->priv != NULL, 0);
	g_return_val_if_fail (credentials->priv->keys != NULL, 0);

	return g_hash_table_size (credentials->priv->keys);
}

static void
gather_key_names (gpointer key, gpointer value, gpointer pslist)
{
	GSList **slist = pslist;

	g_return_if_fail (pslist != NULL);
	g_return_if_fail (key != NULL);

	*slist = g_slist_prepend (*slist, key);
}

/* Returns newly allocated list of key names stored in the credentials strucutre;
   strings are internal credentials values, only the list is newly allocated.
   Free the list with g_slist_free() when no longer needed.
*/
GSList *
e_credentials_list_keys	(const ECredentials *credentials)
{
	GSList *keys = NULL;

	g_return_val_if_fail (credentials != NULL, NULL);
	g_return_val_if_fail (credentials->priv != NULL, NULL);
	g_return_val_if_fail (credentials->priv->keys != NULL, NULL);

	g_hash_table_foreach (credentials->priv->keys, gather_key_names, &keys);

	return g_slist_reverse (keys);
}

/* Removes all keys in once. */
void
e_credentials_clear (ECredentials *credentials)
{
	g_return_if_fail (credentials != NULL);
	g_return_if_fail (credentials->priv != NULL);
	g_return_if_fail (credentials->priv->keys != NULL);
	g_return_if_fail (credentials->priv->peek_keys != NULL);

	g_hash_table_remove_all (credentials->priv->peek_keys);
	g_hash_table_remove_all (credentials->priv->keys);
}

void
e_credentials_clear_peek (ECredentials *credentials)
{
	g_return_if_fail (credentials != NULL);
	g_return_if_fail (credentials->priv != NULL);
	g_return_if_fail (credentials->priv->peek_keys != NULL);

	g_hash_table_remove_all (credentials->priv->peek_keys);
}

void
e_credentials_util_safe_free_string (gchar *str)
{
	if (!str)
		return;

	if (*str)
		memset (str, 0, sizeof (gchar) * strlen (str));

	g_free (str);
}

static struct _PromptFlags {
	ECredentialsPromptFlags flag_uint;
	const gchar *flag_string;
	gboolean is_bit_flag; /* if false, then checked against E_CREDENTIALS_PROMPT_FLAG_REMEMBER_MASK */
} PromptFlags[] = {
	{ E_CREDENTIALS_PROMPT_FLAG_REMEMBER_NEVER,	"remember-never",	FALSE },
	{ E_CREDENTIALS_PROMPT_FLAG_REMEMBER_SESSION,	"remember-session",	FALSE },
	{ E_CREDENTIALS_PROMPT_FLAG_REMEMBER_FOREVER,	"remember-forever",	FALSE },

	{ E_CREDENTIALS_PROMPT_FLAG_SECRET,		"secret",		TRUE },
	{ E_CREDENTIALS_PROMPT_FLAG_REPROMPT,		"reprompt",		TRUE },
	{ E_CREDENTIALS_PROMPT_FLAG_ONLINE,		"online",		TRUE },
	{ E_CREDENTIALS_PROMPT_FLAG_DISABLE_REMEMBER,	"disable-remember",	TRUE },
	{ E_CREDENTIALS_PROMPT_FLAG_PASSPHRASE,		"passphrase",		TRUE }
};

/* Returned pointer can be passed to e_credentials_util_string_to prompt_flags()
   to decode it back to flags. Free returned pointer with g_free().
*/
gchar *
e_credentials_util_prompt_flags_to_string (guint prompt_flags)
{
	gint ii;
	guint masked = prompt_flags & E_CREDENTIALS_PROMPT_FLAG_REMEMBER_MASK;
	GString *str = g_string_new ("");

	for (ii = 0; ii < G_N_ELEMENTS (PromptFlags); ii++) {
		const gchar *add = NULL;

		if (PromptFlags[ii].is_bit_flag) {
			if ((prompt_flags & PromptFlags[ii].flag_uint) != 0)
				add = PromptFlags[ii].flag_string;
		} else if (masked == PromptFlags[ii].flag_uint) {
			add = PromptFlags[ii].flag_string;
		}

		if (!add)
			continue;

		if (str->len)
			g_string_append (str, ",");

		g_string_append (str, add);
	}

	return g_string_free (str, FALSE);
}

guint
e_credentials_util_string_to_prompt_flags (const gchar *prompt_flags_string)
{
	gchar **strv;
	gint ii, jj;
	guint flags = 0;

	if (!prompt_flags_string || !*prompt_flags_string)
		return flags;

	strv = g_strsplit (prompt_flags_string, ",", -1);
	if (!strv)
		return flags;

	for (jj = 0; strv[jj]; jj++) {
		const gchar *str = strv[jj];

		for (ii = 0; ii < G_N_ELEMENTS (PromptFlags); ii++) {
			if (g_str_equal (PromptFlags[ii].flag_string, str)) {
				if (PromptFlags[ii].is_bit_flag)
					flags |= PromptFlags[ii].flag_uint;
				else
					flags = (flags & (~E_CREDENTIALS_PROMPT_FLAG_REMEMBER_MASK)) | PromptFlags[ii].flag_uint;
			}
		}
	}

	g_strfreev (strv);

	return flags;
}

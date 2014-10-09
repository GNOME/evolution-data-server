/*
 * e-source.c
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * SECTION: e-source
 * @include: libedataserver/libedataserver.h
 * @short_description: Hierarchical data sources
 *
 * An #ESource (or "data source") is a description of a file or network
 * location where data can be obtained (such as a mail account), or a
 * description of a resource at that location (such as a mail folder).
 *
 * In more concrete terms, it's an interface for a key file.  All such
 * key files have a main group named [Data Source].  The keys in a
 * [Data Source] group map to #GObject properties in an #ESource.
 *
 * Additional groups in the key file are referred to as "extensions".
 * #ESourceExtension serves as the base class for writing interfaces
 * for these additional key file groups.  The keys in one of these
 * key file groups map to #GObject properties in some custom subclass
 * of #ESourceExtension which was written specifically for that key
 * file group.  For example, a key file might include a group named
 * [Calendar], whose keys map to #GObject properties in an extension
 * class named #ESourceCalendar.
 *
 * Each #ESource contains an internal dictionary of extension objects,
 * accessible by their key file group name.  e_source_get_extension()
 * can look up extension objects by name.
 *
 * An #ESource is identified by a unique identifier string, or "UID",
 * which is also the basename of the corresponding key file.  Additional
 * files related to the #ESource, such as cache files, are usually kept
 * in a directory named after the UID of the #ESource.  Similarly, the
 * password for an account described by an #ESource is kept in GNOME
 * Keyring under the UID of the #ESource.  This makes finding these
 * additional resources simple.
 *
 * Several extensions for common information such as authentication
 * details are built into libedataserver (#ESourceAuthentication, for
 * example).  Backend modules may also define their own extensions for
 * information and settings unique to the backend.  #ESourceExtension
 * subclasses written for specific backends are generally not available
 * to applications and shared libraries.  This is by design, to try and
 * keep backend-specific knowledge from creeping into places it doesn't
 * belong.
 *
 * As of 3.12, an #ESource with an #ESourceProxy extension can serve as a
 * #GProxyResolver.  Calling g_proxy_resolver_is_supported() on an #ESource
 * will reflect this constraint.  Attempting a proxy lookup operation on an
 * #ESource for which g_proxy_resolver_is_supported() returns %FALSE will
 * fail with %G_IO_ERROR_NOT_SUPPORTED.
 **/

#include "e-source.h"

#include <config.h>
#include <string.h>
#include <glib/gi18n-lib.h>

#include <libsecret/secret.h>

/* Private D-Bus classes. */
#include <e-dbus-source.h>

#include "e-data-server-util.h"
#include "e-source-extension.h"
#include "e-uid.h"

/* built-in extension types */
#include "e-source-address-book.h"
#include "e-source-alarms.h"
#include "e-source-authentication.h"
#include "e-source-autocomplete.h"
#include "e-source-calendar.h"
#include "e-source-camel.h"
#include "e-source-collection.h"
#include "e-source-goa.h"
#include "e-source-mail-account.h"
#include "e-source-mail-composition.h"
#include "e-source-mail-identity.h"
#include "e-source-mail-signature.h"
#include "e-source-mail-submission.h"
#include "e-source-mail-transport.h"
#include "e-source-mdn.h"
#include "e-source-offline.h"
#include "e-source-openpgp.h"
#include "e-source-proxy.h"
#include "e-source-refresh.h"
#include "e-source-resource.h"
#include "e-source-revision-guards.h"
#include "e-source-security.h"
#include "e-source-selectable.h"
#include "e-source-smime.h"
#include "e-source-uoa.h"
#include "e-source-webdav.h"

#define E_SOURCE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE, ESourcePrivate))

#define PRIMARY_GROUP_NAME	"Data Source"

#define KEYRING_ITEM_ATTRIBUTE_NAME	"e-source-uid"

typedef struct _AsyncContext AsyncContext;
typedef struct _RemoveContext RemoveContext;

struct _ESourcePrivate {
	GDBusObject *dbus_object;
	GMainContext *main_context;

	GSource *changed;
	GMutex changed_lock;

	GSource *data_change;
	GMutex data_change_lock;

	GMutex property_lock;

	gchar *display_name;
	gchar *collate_key;
	gchar *parent;
	gchar *uid;

	/* The lock guards the key file and hash table. */

	GKeyFile *key_file;
	GRecMutex lock;
	GHashTable *extensions;

	gboolean enabled;
	gboolean initialized;
};

struct _AsyncContext {
	ESource *scratch_source;
	gchar *access_token;
	gint expires_in;
	gchar *password;
	gboolean permanently;
};

/* Used in e_source_remove_sync() */
struct _RemoveContext {
	GMainContext *main_context;
	GMainLoop *main_loop;
};

enum {
	PROP_0,
	PROP_DBUS_OBJECT,
	PROP_DISPLAY_NAME,
	PROP_ENABLED,
	PROP_MAIN_CONTEXT,
	PROP_PARENT,
	PROP_REMOTE_CREATABLE,
	PROP_REMOTE_DELETABLE,
	PROP_REMOVABLE,
	PROP_UID,
	PROP_WRITABLE
};

enum {
	CHANGED,
	LAST_SIGNAL
};

#ifndef G_OS_WIN32

static SecretSchema password_schema = {
	"org.gnome.Evolution.Data.Source",
	SECRET_SCHEMA_DONT_MATCH_NAME,
	{
		{ KEYRING_ITEM_ATTRIBUTE_NAME,
		  SECRET_SCHEMA_ATTRIBUTE_STRING },
		{ NULL, 0 }
	}
};

#endif /* !G_OS_WIN32 */

static guint signals[LAST_SIGNAL];

#ifdef G_OS_WIN32

G_LOCK_DEFINE_STATIC (passwords_file);
#define PASSWORDS_SECTION "Passwords"

static gchar *
encode_password (const gchar *password)
{
	return g_base64_encode ((const guchar *) password, strlen (password));
}

static gchar *
decode_password (const gchar *password)
{
	guchar *decoded;
	gchar *tmp;
	gsize len = 0;

	decoded = g_base64_decode (password, &len);
	if (!decoded || !len) {
		g_free (decoded);
		return NULL;
	}

	tmp = g_strndup ((const gchar *) decoded, len);
	g_free (decoded);

	return tmp;
}

static gchar *
get_passwords_filename (void)
{
	return g_build_filename (e_get_user_config_dir (), "passwords", NULL);
}

static GKeyFile *
read_passwords_file (GError **error)
{
	gchar *filename;
	GKeyFile *passwords;

	passwords = g_key_file_new ();

	filename = get_passwords_filename ();

	if (g_file_test (filename, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
		if (!g_key_file_load_from_file (passwords, filename, G_KEY_FILE_NONE, error)) {
			g_key_file_free (passwords);
			passwords = NULL;
		}
	}

	g_free (filename);

	return passwords;
}

static gboolean
store_passwords_file (GKeyFile *passwords,
		      GError **error)
{
	gchar *content, *filename;
	gsize length;
	gboolean success;

	g_return_val_if_fail (passwords != NULL, FALSE);

	if (!g_file_test (e_get_user_config_dir (), G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		if (g_mkdir_with_parents (e_get_user_config_dir (), 0700) == -1) {
			g_set_error_literal (
				error, G_FILE_ERROR,
				g_file_error_from_errno (errno),
				g_strerror (errno));
			return FALSE;
		}
	}

	content = g_key_file_to_data (passwords, &length, error);
	if (!content)
		return FALSE;


	filename = get_passwords_filename ();

	success = g_file_set_contents (filename, content, length, error);

	g_free (filename);
	g_free (content);

	return success;
}

static gboolean
e_win32_source_store_password_sync (const gchar *uid,
				    const gchar *password,
				    GError **error)
{
	GKeyFile *passwords;
	gboolean success;

	g_return_val_if_fail (uid != NULL, FALSE);

	G_LOCK (passwords_file);

	passwords = read_passwords_file (error);
	success = passwords != NULL;

	if (passwords) {
		gchar *encoded;

		encoded = password && *password ? encode_password (password) : g_strdup (password);

		g_key_file_set_string (passwords, PASSWORDS_SECTION, uid, encoded);

		success = store_passwords_file (passwords, error);

		g_key_file_free (passwords);
		g_free (encoded);
	}

	G_UNLOCK (passwords_file);

	return success;
}

static gchar *
e_win32_source_lookup_password_sync (const gchar *uid,
				     GError **error)
{
	GKeyFile *passwords;
	gchar *password = NULL;

	g_return_val_if_fail (uid != NULL, NULL);

	G_LOCK (passwords_file);

	passwords = read_passwords_file (error);
	if (passwords) {
		gchar *tmp;

		tmp = g_key_file_get_string (passwords, PASSWORDS_SECTION, uid, NULL);
		if (tmp) {
			password = *tmp ? decode_password (tmp) : g_strdup ("");
			g_free (tmp);
		}

		g_key_file_free (passwords);
	}

	G_UNLOCK (passwords_file);

	return password;
}

static gboolean
e_win32_source_delete_password_sync (const gchar *uid,
				     GError **error)
{
	GKeyFile *passwords;
	gboolean success = FALSE;

	g_return_val_if_fail (uid != NULL, FALSE);

	G_LOCK (passwords_file);

	passwords = read_passwords_file (error);
	if (passwords) {
		success = TRUE;

		if (g_key_file_remove_key (passwords, PASSWORDS_SECTION, uid, NULL)) {
			success = store_passwords_file (passwords, error);
		}

		g_key_file_free (passwords);
	}

	G_UNLOCK (passwords_file);

	return success;
}

#endif /* G_OS_WIN32 */

/* Forward Declarations */
static void	e_source_initable_init	(GInitableIface *iface);
static void	e_source_proxy_resolver_init
					(GProxyResolverInterface *iface);

/* Private function shared only with ESourceRegistry. */
void		__e_source_private_replace_dbus_object
						(ESource *source,
						 GDBusObject *dbus_object);

G_DEFINE_TYPE_WITH_CODE (
	ESource,
	e_source,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_source_initable_init)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_PROXY_RESOLVER,
		e_source_proxy_resolver_init))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->scratch_source != NULL)
		g_object_unref (async_context->scratch_source);

	g_free (async_context->access_token);
	g_free (async_context->password);

	g_slice_free (AsyncContext, async_context);
}

static RemoveContext *
remove_context_new (void)
{
	RemoveContext *remove_context;

	remove_context = g_slice_new0 (RemoveContext);

	remove_context->main_context = g_main_context_new ();

	remove_context->main_loop = g_main_loop_new (
		remove_context->main_context, FALSE);

	return remove_context;
}

static void
remove_context_free (RemoveContext *remove_context)
{
	g_main_loop_unref (remove_context->main_loop);
	g_main_context_unref (remove_context->main_context);

	g_slice_free (RemoveContext, remove_context);
}

static void
source_find_extension_classes_rec (GType parent_type,
                                   GHashTable *hash_table)
{
	GType *children;
	guint n_children, ii;

	children = g_type_children (parent_type, &n_children);

	for (ii = 0; ii < n_children; ii++) {
		GType type = children[ii];
		ESourceExtensionClass *class;
		gpointer key;

		/* Recurse over the child's children. */
		source_find_extension_classes_rec (type, hash_table);

		/* Skip abstract types. */
		if (G_TYPE_IS_ABSTRACT (type))
			continue;

		class = g_type_class_ref (type);
		key = (gpointer) class->name;

		if (key != NULL)
			g_hash_table_insert (hash_table, key, class);
		else
			g_type_class_unref (class);
	}

	g_free (children);
}

static GHashTable *
source_find_extension_classes (void)
{
	GHashTable *hash_table;

	hash_table = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) g_type_class_unref);

	source_find_extension_classes_rec (
		E_TYPE_SOURCE_EXTENSION, hash_table);

	return hash_table;
}

static void
source_localized_hack (GKeyFile *key_file,
                       const gchar *group_name,
                       const gchar *key,
                       const gchar *new_value)
{
	const gchar * const *language_names;
	gint ii;

	/* XXX If we're changing a string key that has translations,
	 *     set "key[$CURRENT_LOCALE]" (if available) to the new
	 *     value so g_key_file_get_locale_string() will pick it
	 *     up.  This is not a perfect solution however.  When a
	 *     different locale is used the value may revert to its
	 *     original localized string.  Good enough for now. */

	language_names = g_get_language_names ();

	for (ii = 0; language_names[ii] != NULL; ii++) {
		gboolean has_localized_key;
		gchar *localized_key;

		localized_key = g_strdup_printf (
			"%s[%s]", key, language_names[ii]);
		has_localized_key = g_key_file_has_key (
			key_file, group_name, localized_key, NULL);

		if (has_localized_key)
			g_key_file_set_string (
				key_file, group_name,
				localized_key, new_value);

		g_free (localized_key);

		if (has_localized_key)
			return;
	}

	g_key_file_set_string (key_file, group_name, key, new_value);
}

static gboolean
source_check_values_differ (GType g_type,
                            const GValue *value,
                            const GValue *value2)
{
	gboolean differ = TRUE;
	GValue *value1;

	g_return_val_if_fail (value != NULL, TRUE);
	g_return_val_if_fail (value2 != NULL, TRUE);

	value1 = g_slice_new0 (GValue);
	g_value_init (value1, g_type);
	g_value_copy (value2, value1);

	if (g_value_transform (value, value1)) {
		#define check_type(name,get) G_STMT_START { \
			if (G_VALUE_HOLDS_ ## name (value1)) { \
				differ = g_value_get_ ## get (value1) != g_value_get_ ## get (value2); \
				break; \
			} } G_STMT_END

		do {
			check_type (BOOLEAN, boolean);
			check_type (CHAR, schar);
			check_type (DOUBLE, double);
			check_type (ENUM, enum);
			check_type (FLAGS, flags);
			check_type (FLOAT, float);
			check_type (GTYPE, gtype);
			check_type (INT, int);
			check_type (INT64, int64);
			check_type (LONG, long);
			check_type (POINTER, pointer);
			check_type (UCHAR, uchar);
			check_type (UINT, uint);
			check_type (UINT64, uint64);
			check_type (ULONG, ulong);

			if (G_VALUE_HOLDS_STRING (value1)) {
				differ = g_strcmp0 (g_value_get_string (value1), g_value_get_string (value2)) != 0;
				break;
			}

			if (G_VALUE_HOLDS_VARIANT (value1)) {
				GVariant *variant1, *variant2;

				variant1 = g_value_get_variant (value1);
				variant2 = g_value_get_variant (value2);
				differ = g_variant_compare (variant1, variant2) != 0;
				break;
			}
		} while (FALSE);

		#undef check_type
	}

	g_value_unset (value1);
	g_slice_free (GValue, value1);

	return differ;
}

static void
source_set_key_file_from_property (GObject *object,
                                   GParamSpec *pspec,
                                   GKeyFile *key_file,
                                   const gchar *group_name)
{
	GValue *pvalue;
	GValue *svalue;
	gchar *key;

	pvalue = g_slice_new0 (GValue);
	g_value_init (pvalue, pspec->value_type);
	g_object_get_property (object, pspec->name, pvalue);

	svalue = g_slice_new0 (GValue);
	g_value_init (svalue, G_TYPE_STRING);

	key = e_source_parameter_to_key (pspec->name);

	/* For the most part we can just transform any supported
	 * property type to a string, with a couple exceptions. */

	/* Transforming a boolean GValue to a string results in
	 * "TRUE" or "FALSE" (all uppercase), but GKeyFile only
	 * recognizes "true" or "false" (all lowercase).  So we
	 * have to use g_key_file_set_boolean(). */
	if (G_VALUE_HOLDS_BOOLEAN (pvalue)) {
		gboolean v_boolean = g_value_get_boolean (pvalue);
		g_key_file_set_boolean (key_file, group_name, key, v_boolean);

	/* Store UIN64 in hexa */
	} else if (G_VALUE_HOLDS_UINT64 (pvalue)) {
		gchar *v_str;

		v_str = g_strdup_printf (
			"%016" G_GINT64_MODIFIER "X",
			g_value_get_uint64 (pvalue));
		g_key_file_set_string (key_file, group_name, key, v_str);
		g_free (v_str);

	/* String GValues may contain characters that need escaping. */
	} else if (G_VALUE_HOLDS_STRING (pvalue)) {
		const gchar *v_string = g_value_get_string (pvalue);

		if (v_string == NULL)
			v_string = "";

		/* Special case for localized "DisplayName" keys. */
		source_localized_hack (key_file, group_name, key, v_string);

	/* Transforming an enum GValue to a string results in
	 * the GEnumValue name.  We want the shorter nickname. */
	} else if (G_VALUE_HOLDS_ENUM (pvalue)) {
		GParamSpecEnum *enum_pspec;
		GEnumClass *enum_class;
		GEnumValue *enum_value;
		gint value;

		enum_pspec = G_PARAM_SPEC_ENUM (pspec);
		enum_class = enum_pspec->enum_class;

		value = g_value_get_enum (pvalue);
		enum_value = g_enum_get_value (enum_class, value);

		if (enum_value == NULL) {
			value = enum_pspec->default_value;
			enum_value = g_enum_get_value (enum_class, value);
		}

		if (enum_value != NULL)
			g_key_file_set_string (
				key_file, group_name, key,
				enum_value->value_nick);

	} else if (G_VALUE_HOLDS (pvalue, G_TYPE_STRV)) {
		const gchar **strv = g_value_get_boxed (pvalue);
		guint length = 0;

		if (strv != NULL)
			length = g_strv_length ((gchar **) strv);
		g_key_file_set_string_list (
			key_file, group_name, key, strv, length);

	/* For GValues holding a GFile object we save the URI. */
	} else if (G_VALUE_HOLDS (pvalue, G_TYPE_FILE)) {
		GFile *file = g_value_get_object (pvalue);
		gchar *uri = NULL;

		if (file != NULL)
			uri = g_file_get_uri (file);
		g_key_file_set_string (
			key_file, group_name, key,
			(uri != NULL) ? uri : "");
		g_free (uri);

	} else if (g_value_transform (pvalue, svalue)) {
		const gchar *value = g_value_get_string (svalue);
		g_key_file_set_value (key_file, group_name, key, value);
	}

	g_free (key);
	g_value_unset (pvalue);
	g_value_unset (svalue);
	g_slice_free (GValue, pvalue);
	g_slice_free (GValue, svalue);
}

static void
source_set_property_from_key_file (GObject *object,
                                   GParamSpec *pspec,
                                   GKeyFile *key_file,
                                   const gchar *group_name)
{
	gchar *key;
	GValue *value;
	GError *local_error = NULL;

	value = g_slice_new0 (GValue);
	key = e_source_parameter_to_key (pspec->name);

	if (G_IS_PARAM_SPEC_CHAR (pspec) ||
	    G_IS_PARAM_SPEC_UCHAR (pspec) ||
	    G_IS_PARAM_SPEC_INT (pspec) ||
	    G_IS_PARAM_SPEC_UINT (pspec) ||
	    G_IS_PARAM_SPEC_LONG (pspec) ||
	    G_IS_PARAM_SPEC_ULONG (pspec)) {
		gint v_int;

		v_int = g_key_file_get_integer (
			key_file, group_name, key, &local_error);
		if (local_error == NULL) {
			g_value_init (value, G_TYPE_INT);
			g_value_set_int (value, v_int);
		}

	} else if (G_IS_PARAM_SPEC_INT64 (pspec)) {
		gint64 v_int64;

		v_int64 = g_key_file_get_int64 (
			key_file, group_name, key, &local_error);
		if (local_error == NULL) {
			g_value_init (value, G_TYPE_INT64);
			g_value_set_int64 (value, v_int64);
		}

	} else if (G_IS_PARAM_SPEC_UINT64 (pspec)) {
		guint64 v_uint64;
		gchar *v_str;

		v_str = g_key_file_get_string (
			key_file, group_name, key, &local_error);
		if (local_error == NULL) {
			v_uint64 = g_ascii_strtoull (v_str, NULL, 16);

			g_value_init (value, G_TYPE_UINT64);
			g_value_set_uint64 (value, v_uint64);
		}

		g_free (v_str);

	} else if (G_IS_PARAM_SPEC_BOOLEAN (pspec)) {
		gboolean v_boolean;

		v_boolean = g_key_file_get_boolean (
			key_file, group_name, key, &local_error);
		if (local_error == NULL) {
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, v_boolean);
		}

	} else if (G_IS_PARAM_SPEC_ENUM (pspec)) {
		gchar *nick;

		nick = g_key_file_get_string (
			key_file, group_name, key, &local_error);
		if (local_error == NULL) {
			GParamSpecEnum *enum_pspec;
			GEnumValue *enum_value;

			enum_pspec = G_PARAM_SPEC_ENUM (pspec);
			enum_value = g_enum_get_value_by_nick (
				enum_pspec->enum_class, nick);
			if (enum_value != NULL) {
				g_value_init (value, pspec->value_type);
				g_value_set_enum (value, enum_value->value);
			}
			g_free (nick);
		}

	} else if (G_IS_PARAM_SPEC_FLOAT (pspec) ||
		   G_IS_PARAM_SPEC_DOUBLE (pspec)) {
		gdouble v_double;

		v_double = g_key_file_get_double (
			key_file, group_name, key, &local_error);
		if (local_error == NULL) {
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, v_double);
		}

	} else if (G_IS_PARAM_SPEC_STRING (pspec)) {
		gchar *v_string;

		/* Get the localized string if present. */
		v_string = g_key_file_get_locale_string (
			key_file, group_name, key, NULL, &local_error);
		if (local_error == NULL) {
			g_value_init (value, G_TYPE_STRING);
			g_value_take_string (value, v_string);
		}

	} else if (g_type_is_a (pspec->value_type, G_TYPE_STRV)) {
		gchar **strv;

		strv = g_key_file_get_string_list (
			key_file, group_name, key, NULL, &local_error);
		if (local_error == NULL) {
			g_value_init (value, G_TYPE_STRV);
			g_value_take_boxed (value, strv);
		}

	} else if (g_type_is_a (pspec->value_type, G_TYPE_FILE)) {
		gchar *uri;

		/* Create the GFile from the URI string. */
		uri = g_key_file_get_locale_string (
			key_file, group_name, key, NULL, &local_error);
		if (local_error == NULL) {
			GFile *file = NULL;
			if (uri != NULL && *uri != '\0')
				file = g_file_new_for_uri (uri);
			g_value_init (value, pspec->value_type);
			g_value_take_object (value, file);
			g_free (uri);
		}

	} else {
		g_warning (
			"No GKeyFile-to-GValue converter defined "
			"for type '%s'", G_PARAM_SPEC_TYPE_NAME (pspec));
	}

	/* If a value could not be retrieved from the key
	 * file, restore the property to its default value. */
	if (local_error != NULL) {
		g_value_init (value, pspec->value_type);
		g_param_value_set_default (pspec, value);
		g_error_free (local_error);
	}

	if (G_IS_VALUE (value)) {
		GValue *cvalue;

		cvalue = g_slice_new0 (GValue);
		g_value_init (cvalue, pspec->value_type);
		g_object_get_property (object, pspec->name, cvalue);

		/* This is because the g_object_set_property() invokes "notify" signal
		 * on the set property, even if the value did not change, which creates
		 * false notifications, which can cause UI or background activities
		 * without any real reason (especially with the ''enabled' property load). */
		if (!G_IS_VALUE (cvalue) || source_check_values_differ (pspec->value_type, value, cvalue))
			g_object_set_property (object, pspec->name, value);

		if (G_IS_VALUE (cvalue))
			g_value_unset (cvalue);
		g_slice_free (GValue, cvalue);

		g_value_unset (value);
	}

	g_slice_free (GValue, value);
	g_free (key);
}

static void
source_load_from_key_file (GObject *object,
                           GKeyFile *key_file,
                           const gchar *group_name)
{
	GObjectClass *class;
	GParamSpec **properties;
	guint n_properties, ii;

	class = G_OBJECT_GET_CLASS (object);
	properties = g_object_class_list_properties (class, &n_properties);

	g_object_freeze_notify (object);

	for (ii = 0; ii < n_properties; ii++) {
		if (properties[ii]->flags & E_SOURCE_PARAM_SETTING) {
			source_set_property_from_key_file (
				object, properties[ii], key_file, group_name);
		}
	}

	g_object_thaw_notify (object);

	g_free (properties);
}

static void
source_save_to_key_file (GObject *object,
                         GKeyFile *key_file,
                         const gchar *group_name)
{
	GObjectClass *class;
	GParamSpec **properties;
	guint n_properties, ii;

	class = G_OBJECT_GET_CLASS (object);
	properties = g_object_class_list_properties (class, &n_properties);

	for (ii = 0; ii < n_properties; ii++) {
		if (properties[ii]->flags & E_SOURCE_PARAM_SETTING) {
			source_set_key_file_from_property (
				object, properties[ii], key_file, group_name);
		}
	}

	g_free (properties);
}

static gboolean
source_parse_dbus_data (ESource *source,
                        GError **error)
{
	GHashTableIter iter;
	EDBusObject *dbus_object;
	EDBusSource *dbus_source;
	GKeyFile *key_file;
	gpointer group_name;
	gpointer extension;
	gchar *data;
	gboolean success;

	dbus_object = E_DBUS_OBJECT (source->priv->dbus_object);

	dbus_source = e_dbus_object_get_source (dbus_object);
	data = e_dbus_source_dup_data (dbus_source);
	g_object_unref (dbus_source);

	g_return_val_if_fail (data != NULL, FALSE);

	key_file = source->priv->key_file;

	success = g_key_file_load_from_data (
		key_file, data, strlen (data),
		G_KEY_FILE_KEEP_COMMENTS |
		G_KEY_FILE_KEEP_TRANSLATIONS,
		error);

	g_free (data);
	data = NULL;

	if (!success)
		return FALSE;

	/* Make sure the key file has a [Data Source] group. */
	if (!g_key_file_has_group (key_file, PRIMARY_GROUP_NAME)) {
		g_set_error (
			error, G_KEY_FILE_ERROR,
			G_KEY_FILE_ERROR_GROUP_NOT_FOUND,
			_("Source file is missing a [%s] group"),
			PRIMARY_GROUP_NAME);
		return FALSE;
	}

	/* Load key file values from the [Data Source] group and from
	 * any other groups for which an extension object has already
	 * been created.  Note that not all the extension classes may
	 * be registered at this point, so avoid attempting to create
	 * new extension objects here.  Extension objects are created
	 * on-demand in e_source_get_extension(). */

	source_load_from_key_file (
		G_OBJECT (source), key_file, PRIMARY_GROUP_NAME);

	g_hash_table_iter_init (&iter, source->priv->extensions);
	while (g_hash_table_iter_next (&iter, &group_name, &extension))
		source_load_from_key_file (extension, key_file, group_name);

	return TRUE;
}

static gboolean
source_idle_data_change_cb (gpointer user_data)
{
	ESource *source = E_SOURCE (user_data);
	GError *local_error = NULL;

	/* If the ESource is still initializing itself in a different
	 * thread, skip the signal emission and try again on the next
	 * main loop iteration. This is a busy wait but it should be
	 * a very short wait. */
	if (!source->priv->initialized)
		return TRUE;

	g_mutex_lock (&source->priv->data_change_lock);
	if (source->priv->data_change != NULL) {
		g_source_unref (source->priv->data_change);
		source->priv->data_change = NULL;
	}
	g_mutex_unlock (&source->priv->data_change_lock);

	g_rec_mutex_lock (&source->priv->lock);

	/* Since the source data came from a GKeyFile structure on the
	 * server-side, this should never fail.  But we'll print error
	 * messages to the terminal just in case. */
	source_parse_dbus_data (source, &local_error);

	if (local_error != NULL) {
		g_warning ("%s", local_error->message);
		g_error_free (local_error);
	}

	g_rec_mutex_unlock (&source->priv->lock);

	return FALSE;
}

static void
source_notify_dbus_data_cb (EDBusSource *dbus_source,
                            GParamSpec *pspec,
                            ESource *source)
{
	g_mutex_lock (&source->priv->data_change_lock);
	if (source->priv->data_change == NULL) {
		source->priv->data_change = g_idle_source_new ();
		g_source_set_callback (
			source->priv->data_change,
			source_idle_data_change_cb,
			source, NULL);
		g_source_attach (
			source->priv->data_change,
			source->priv->main_context);
	}
	g_mutex_unlock (&source->priv->data_change_lock);
}

static gboolean
source_idle_changed_cb (gpointer user_data)
{
	ESource *source = E_SOURCE (user_data);

	/* If the ESource is still initializing itself in a different
	 * thread, skip the signal emission and try again on the next
	 * main loop iteration.  This is a busy wait but it should be
	 * a very short wait. */
	if (!source->priv->initialized)
		return TRUE;

	g_mutex_lock (&source->priv->changed_lock);
	if (source->priv->changed != NULL) {
		g_source_unref (source->priv->changed);
		source->priv->changed = NULL;
	}
	g_mutex_unlock (&source->priv->changed_lock);

	g_signal_emit (source, signals[CHANGED], 0);

	return FALSE;
}

static void
source_set_dbus_object (ESource *source,
                        EDBusObject *dbus_object)
{
	/* D-Bus object will be NULL when configuring a new source. */
	if (dbus_object == NULL)
		return;

	g_return_if_fail (E_DBUS_IS_OBJECT (dbus_object));
	g_return_if_fail (source->priv->dbus_object == NULL);

	source->priv->dbus_object = g_object_ref (dbus_object);
}

static void
source_set_main_context (ESource *source,
                         GMainContext *main_context)
{
	g_return_if_fail (source->priv->main_context == NULL);

	source->priv->main_context =
		(main_context != NULL) ?
		g_main_context_ref (main_context) :
		g_main_context_ref_thread_default ();
}

static void
source_set_uid (ESource *source,
                const gchar *uid)
{
	/* The "uid" argument will usually be NULL unless called
	 * from e_source_new_with_uid().  If NULL, we'll pick up
	 * a UID in source_initable_init(). */

	g_return_if_fail (source->priv->uid == NULL);

	source->priv->uid = g_strdup (uid);
}

static void
source_set_property (GObject *object,
                     guint property_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DBUS_OBJECT:
			source_set_dbus_object (
				E_SOURCE (object),
				g_value_get_object (value));
			return;

		case PROP_DISPLAY_NAME:
			e_source_set_display_name (
				E_SOURCE (object),
				g_value_get_string (value));
			return;

		case PROP_ENABLED:
			e_source_set_enabled (
				E_SOURCE (object),
				g_value_get_boolean (value));
			return;

		case PROP_MAIN_CONTEXT:
			source_set_main_context (
				E_SOURCE (object),
				g_value_get_boxed (value));
			return;

		case PROP_PARENT:
			e_source_set_parent (
				E_SOURCE (object),
				g_value_get_string (value));
			return;

		case PROP_UID:
			source_set_uid (
				E_SOURCE (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_get_property (GObject *object,
                     guint property_id,
                     GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DBUS_OBJECT:
			g_value_take_object (
				value, e_source_ref_dbus_object (
				E_SOURCE (object)));
			return;

		case PROP_DISPLAY_NAME:
			g_value_take_string (
				value, e_source_dup_display_name (
				E_SOURCE (object)));
			return;

		case PROP_ENABLED:
			g_value_set_boolean (
				value, e_source_get_enabled (
				E_SOURCE (object)));
			return;

		case PROP_MAIN_CONTEXT:
			g_value_take_boxed (
				value, e_source_ref_main_context (
				E_SOURCE (object)));
			return;

		case PROP_PARENT:
			g_value_take_string (
				value, e_source_dup_parent (
				E_SOURCE (object)));
			return;

		case PROP_REMOTE_CREATABLE:
			g_value_set_boolean (
				value, e_source_get_remote_creatable (
				E_SOURCE (object)));
			return;

		case PROP_REMOTE_DELETABLE:
			g_value_set_boolean (
				value, e_source_get_remote_deletable (
				E_SOURCE (object)));
			return;

		case PROP_REMOVABLE:
			g_value_set_boolean (
				value, e_source_get_removable (
				E_SOURCE (object)));
			return;

		case PROP_UID:
			g_value_take_string (
				value, e_source_dup_uid (
				E_SOURCE (object)));
			return;

		case PROP_WRITABLE:
			g_value_set_boolean (
				value, e_source_get_writable (
				E_SOURCE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_dispose (GObject *object)
{
	ESourcePrivate *priv;

	priv = E_SOURCE_GET_PRIVATE (object);

	if (priv->dbus_object != NULL) {
		EDBusObject *dbus_object;
		EDBusSource *dbus_source;

		dbus_object = E_DBUS_OBJECT (priv->dbus_object);

		dbus_source = e_dbus_object_get_source (dbus_object);
		if (dbus_source != NULL) {
			g_signal_handlers_disconnect_matched (
				dbus_source, G_SIGNAL_MATCH_DATA,
				0, 0, NULL, NULL, object);
			g_object_unref (dbus_source);
		}

		g_object_unref (priv->dbus_object);
		priv->dbus_object = NULL;
	}

	if (priv->main_context != NULL) {
		g_main_context_unref (priv->main_context);
		priv->main_context = NULL;
	}

	/* XXX Maybe not necessary to acquire the lock? */
	g_mutex_lock (&priv->changed_lock);
	if (priv->changed != NULL) {
		g_source_destroy (priv->changed);
		g_source_unref (priv->changed);
		priv->changed = NULL;
	}
	g_mutex_unlock (&priv->changed_lock);

	g_mutex_lock (&priv->data_change_lock);
	if (priv->data_change != NULL) {
		g_source_destroy (priv->data_change);
		g_source_unref (priv->data_change);
		priv->data_change = NULL;
	}
	g_mutex_unlock (&priv->data_change_lock);

	g_hash_table_remove_all (priv->extensions);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_source_parent_class)->dispose (object);
}

static void
source_finalize (GObject *object)
{
	ESourcePrivate *priv;

	priv = E_SOURCE_GET_PRIVATE (object);

	g_mutex_clear (&priv->changed_lock);
	g_mutex_clear (&priv->data_change_lock);
	g_mutex_clear (&priv->property_lock);

	g_free (priv->display_name);
	g_free (priv->collate_key);
	g_free (priv->parent);
	g_free (priv->uid);

	g_key_file_free (priv->key_file);
	g_rec_mutex_clear (&priv->lock);
	g_hash_table_destroy (priv->extensions);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_parent_class)->finalize (object);
}

static void
source_notify (GObject *object,
               GParamSpec *pspec)
{
	if ((pspec->flags & E_SOURCE_PARAM_SETTING) != 0)
		e_source_changed (E_SOURCE (object));
}

/* Helper for source_remove_sync() */
static gboolean
source_remove_main_loop_quit_cb (gpointer user_data)
{
	GMainLoop *main_loop = user_data;

	g_main_loop_quit (main_loop);

	return FALSE;
}

/* Helper for e_source_remove_sync() */
static void
source_remove_notify_dbus_object_cb (ESource *source,
                                     GParamSpec *pspec,
                                     RemoveContext *remove_context)
{
	GDBusObject *dbus_object;

	dbus_object = e_source_ref_dbus_object (source);

	/* The GDBusObject will be NULL once the ESourceRegistry
	 * receives an "object-removed" signal for this ESource. */
	if (dbus_object == NULL) {
		GSource *idle_source;

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			source_remove_main_loop_quit_cb,
			g_main_loop_ref (remove_context->main_loop),
			(GDestroyNotify) g_main_loop_unref);
		g_source_attach (idle_source, remove_context->main_context);
		g_source_unref (idle_source);
	}

	g_clear_object (&dbus_object);
}

static gboolean
source_remove_sync (ESource *source,
                    GCancellable *cancellable,
                    GError **error)
{
	EDBusSourceRemovable *dbus_interface = NULL;
	GDBusObject *dbus_object;
	RemoveContext *remove_context;
	gulong notify_dbus_object_id;
	GError *local_error = NULL;

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		dbus_interface =
			e_dbus_object_get_source_removable (
			E_DBUS_OBJECT (dbus_object));
		g_object_unref (dbus_object);
	}

	if (dbus_interface == NULL) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_PERMISSION_DENIED,
			_("Data source '%s' is not removable"),
			e_source_get_display_name (source));
		return FALSE;
	}

	remove_context = remove_context_new ();
	g_main_context_push_thread_default (remove_context->main_context);

	notify_dbus_object_id = g_signal_connect (
		source, "notify::dbus-object",
		G_CALLBACK (source_remove_notify_dbus_object_cb),
		remove_context);

	e_dbus_source_removable_call_remove_sync (
		dbus_interface, cancellable, &local_error);

	/* Wait for the ESourceRegistry to remove our GDBusObject while
	 * handling an "object-removed" signal from the registry service.
	 * But also set a short timeout to avoid getting deadlocked here. */
	if (local_error == NULL) {
		GSource *timeout_source;

		timeout_source = g_timeout_source_new_seconds (2);
		g_source_set_callback (
			timeout_source,
			source_remove_main_loop_quit_cb,
			g_main_loop_ref (remove_context->main_loop),
			(GDestroyNotify) g_main_loop_unref);
		g_source_attach (timeout_source, remove_context->main_context);
		g_source_unref (timeout_source);

		g_main_loop_run (remove_context->main_loop);
	}

	g_signal_handler_disconnect (source, notify_dbus_object_id);

	g_main_context_pop_thread_default (remove_context->main_context);
	remove_context_free (remove_context);

	g_object_unref (dbus_interface);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for source_remove() */
static void
source_remove_thread (GSimpleAsyncResult *simple,
                      GObject *object,
                      GCancellable *cancellable)
{
	GError *local_error = NULL;

	e_source_remove_sync (E_SOURCE (object), cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

static void
source_remove (ESource *source,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback, user_data, source_remove);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, source_remove_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static gboolean
source_remove_finish (ESource *source,
                      GAsyncResult *result,
                      GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source), source_remove), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
source_write_sync (ESource *source,
                   GCancellable *cancellable,
                   GError **error)
{
	EDBusSourceWritable *dbus_interface = NULL;
	GDBusObject *dbus_object;
	gchar *data;
	GError *local_error = NULL;

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		dbus_interface =
			e_dbus_object_get_source_writable (
			E_DBUS_OBJECT (dbus_object));
		g_object_unref (dbus_object);
	}

	if (dbus_interface == NULL) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_PERMISSION_DENIED,
			_("Data source '%s' is not writable"),
			e_source_get_display_name (source));
		return FALSE;
	}

	data = e_source_to_string (source, NULL);

	e_dbus_source_writable_call_write_sync (
		dbus_interface, data, cancellable, &local_error);

	g_free (data);

	g_object_unref (dbus_interface);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for source_write() */
static void
source_write_thread (GSimpleAsyncResult *simple,
                     GObject *object,
                     GCancellable *cancellable)
{
	GError *local_error = NULL;

	e_source_write_sync (E_SOURCE (object), cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

static void
source_write (ESource *source,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback, user_data, source_write);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, source_write_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static gboolean
source_write_finish (ESource *source,
                     GAsyncResult *result,
                     GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source), source_write), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
source_remote_create_sync (ESource *source,
                           ESource *scratch_source,
                           GCancellable *cancellable,
                           GError **error)
{
	EDBusSourceRemoteCreatable *dbus_interface = NULL;
	GDBusObject *dbus_object;
	gchar *uid, *data;
	GError *local_error = NULL;

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		dbus_interface =
			e_dbus_object_get_source_remote_creatable (
			E_DBUS_OBJECT (dbus_object));
		g_object_unref (dbus_object);
	}

	if (dbus_interface == NULL) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_NOT_SUPPORTED,
			_("Data source '%s' does not "
			"support creating remote resources"),
			e_source_get_display_name (source));
		return FALSE;
	}

	uid = e_source_dup_uid (scratch_source);
	data = e_source_to_string (scratch_source, NULL);

	e_dbus_source_remote_creatable_call_create_sync (
		dbus_interface, uid, data, cancellable, &local_error);

	g_free (data);
	g_free (uid);

	g_object_unref (dbus_interface);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for source_remote_create() */
static void
source_remote_create_thread (GSimpleAsyncResult *simple,
                             GObject *object,
                             GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_source_remote_create_sync (
		E_SOURCE (object),
		async_context->scratch_source,
		cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

static void
source_remote_create (ESource *source,
                      ESource *scratch_source,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->scratch_source = g_object_ref (scratch_source);

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback,
		user_data, source_remote_create);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, source_remote_create_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static gboolean
source_remote_create_finish (ESource *source,
                             GAsyncResult *result,
                             GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source), source_remote_create), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
source_remote_delete_sync (ESource *source,
                           GCancellable *cancellable,
                           GError **error)
{
	EDBusSourceRemoteDeletable *dbus_interface = NULL;
	GDBusObject *dbus_object;
	GError *local_error = NULL;

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		dbus_interface =
			e_dbus_object_get_source_remote_deletable (
			E_DBUS_OBJECT (dbus_object));
		g_object_unref (dbus_object);
	}

	if (dbus_interface == NULL) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_NOT_SUPPORTED,
			_("Data source '%s' does not "
			"support deleting remote resources"),
			e_source_get_display_name (source));
		return FALSE;
	}

	e_dbus_source_remote_deletable_call_delete_sync (
		dbus_interface, cancellable, &local_error);

	g_object_unref (dbus_interface);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for source_remote_delete() */
static void
source_remote_delete_thread (GSimpleAsyncResult *simple,
                             GObject *object,
                             GCancellable *cancellable)
{
	GError *local_error = NULL;

	e_source_remote_delete_sync (
		E_SOURCE (object), cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

static void
source_remote_delete (ESource *source,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback,
		user_data, source_remote_delete);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, source_remote_delete_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static gboolean
source_remote_delete_finish (ESource *source,
                             GAsyncResult *result,
                             GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source), source_remote_delete), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
source_get_oauth2_access_token_sync (ESource *source,
                                     GCancellable *cancellable,
                                     gchar **out_access_token,
                                     gint *out_expires_in,
                                     GError **error)
{
	EDBusSourceOAuth2Support *dbus_interface = NULL;
	GDBusObject *dbus_object;
	GError *local_error = NULL;

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		dbus_interface =
			e_dbus_object_get_source_oauth2_support (
			E_DBUS_OBJECT (dbus_object));
		g_object_unref (dbus_object);
	}

	if (dbus_interface == NULL) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_NOT_SUPPORTED,
			_("Data source '%s' does not "
			"support OAuth 2.0 authentication"),
			e_source_get_display_name (source));
		return FALSE;
	}

	e_dbus_source_oauth2_support_call_get_access_token_sync (
		dbus_interface, out_access_token,
		out_expires_in, cancellable, &local_error);

	g_object_unref (dbus_interface);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for source_get_oauth2_access_token() */
static void
source_get_oauth2_access_token_thread (GSimpleAsyncResult *simple,
                                       GObject *object,
                                       GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_source_get_oauth2_access_token_sync (
		E_SOURCE (object), cancellable,
		&async_context->access_token,
		&async_context->expires_in,
		&local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

static void
source_get_oauth2_access_token (ESource *source,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback, user_data,
		source_get_oauth2_access_token);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, source_get_oauth2_access_token_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static gboolean
source_get_oauth2_access_token_finish (ESource *source,
                                       GAsyncResult *result,
                                       gchar **out_access_token,
                                       gint *out_expires_in,
                                       GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source),
		source_get_oauth2_access_token), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	g_return_val_if_fail (async_context->access_token != NULL, FALSE);

	if (out_access_token != NULL) {
		*out_access_token = async_context->access_token;
		async_context->access_token = NULL;
	}

	if (out_expires_in != NULL)
		*out_expires_in = async_context->expires_in;

	return TRUE;
}

static gboolean
source_initable_init (GInitable *initable,
                      GCancellable *cancellable,
                      GError **error)
{
	ESource *source;
	gboolean success = TRUE;

	source = E_SOURCE (initable);

	/* The D-Bus object has the unique identifier (UID). */
	if (source->priv->dbus_object != NULL) {
		EDBusObject *dbus_object;
		EDBusSource *dbus_source;

		dbus_object = E_DBUS_OBJECT (source->priv->dbus_object);

		/* An EDBusObject lacking an EDBusSource
		 * interface indicates a programmer error. */
		dbus_source = e_dbus_object_get_source (dbus_object);
		g_return_val_if_fail (E_DBUS_IS_SOURCE (dbus_source), FALSE);

		/* The UID never changes, so we can cache a copy.
		 *
		 * XXX Note, EServerSideSource may have already set this
		 *     by way of the "uid" construct-only property, hence
		 *     the g_free() call.  Not a problem, we'll just free
		 *     our UID string and set it to the same value again. */
		g_free (source->priv->uid);
		source->priv->uid = e_dbus_source_dup_uid (dbus_source);

		g_signal_connect (
			dbus_source, "notify::data",
			G_CALLBACK (source_notify_dbus_data_cb), source);

		success = source_parse_dbus_data (source, error);

		g_object_unref (dbus_source);

	/* No D-Bus object implies we're configuring a new source,
	 * so generate a new unique identifier (UID) unless one was
	 * explicitly provided through e_source_new_with_uid(). */
	} else if (source->priv->uid == NULL) {
		source->priv->uid = e_uid_new ();
	}

	/* Try to avoid a spurious "changed" emission. */
	g_mutex_lock (&source->priv->changed_lock);
	if (source->priv->changed != NULL) {
		g_source_destroy (source->priv->changed);
		g_source_unref (source->priv->changed);
		source->priv->changed = NULL;
	}
	g_mutex_unlock (&source->priv->changed_lock);

	source->priv->initialized = TRUE;

	return success;
}

static gboolean
source_proxy_resolver_is_supported (GProxyResolver *resolver)
{
	return e_source_has_extension (
		E_SOURCE (resolver), E_SOURCE_EXTENSION_PROXY);
}

static gchar **
source_proxy_resolver_lookup (GProxyResolver *resolver,
                              const gchar *uri,
                              GCancellable *cancellable,
                              GError **error)
{
	return e_source_proxy_lookup_sync (
		E_SOURCE (resolver), uri, cancellable, error);
}

/* Helper for source_proxy_resolver_lookup_async() */
static void
source_proxy_resolver_lookup_ready_cb (GObject *object,
                                       GAsyncResult *result,
                                       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	gchar **proxies;
	GError *local_error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);

	proxies = e_source_proxy_lookup_finish (
		E_SOURCE (object), result, &local_error);

	/* Sanity check. */
	g_return_if_fail (
		((proxies != NULL) && (local_error == NULL)) ||
		((proxies == NULL) && (local_error != NULL)));

	if (proxies != NULL) {
		g_simple_async_result_set_op_res_gpointer (
			simple, proxies, (GDestroyNotify) g_strfreev);
	} else {
		g_simple_async_result_take_error (simple, local_error);
	}

	g_simple_async_result_complete (simple);

	g_object_unref (simple);
}

static void
source_proxy_resolver_lookup_async (GProxyResolver *resolver,
                                    const gchar *uri,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (resolver), callback, user_data,
		source_proxy_resolver_lookup_async);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	e_source_proxy_lookup (
		E_SOURCE (resolver), uri, cancellable,
		source_proxy_resolver_lookup_ready_cb,
		g_object_ref (simple));

	g_object_unref (simple);
}

static gchar **
source_proxy_resolver_lookup_finish (GProxyResolver *resolver,
                                     GAsyncResult *result,
                                     GError **error)
{
	GSimpleAsyncResult *simple;
	gchar **proxies;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (resolver),
		source_proxy_resolver_lookup_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	proxies = g_simple_async_result_get_op_res_gpointer (simple);

	return g_strdupv (proxies);
}

static void
e_source_class_init (ESourceClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ESourcePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_set_property;
	object_class->get_property = source_get_property;
	object_class->dispose = source_dispose;
	object_class->finalize = source_finalize;
	object_class->notify = source_notify;

	class->remove_sync = source_remove_sync;
	class->remove = source_remove;
	class->remove_finish = source_remove_finish;
	class->write_sync = source_write_sync;
	class->write = source_write;
	class->write_finish = source_write_finish;
	class->remote_create_sync = source_remote_create_sync;
	class->remote_create = source_remote_create;
	class->remote_create_finish = source_remote_create_finish;
	class->remote_delete_sync = source_remote_delete_sync;
	class->remote_delete = source_remote_delete;
	class->remote_delete_finish = source_remote_delete_finish;
	class->get_oauth2_access_token_sync = source_get_oauth2_access_token_sync;
	class->get_oauth2_access_token = source_get_oauth2_access_token;
	class->get_oauth2_access_token_finish = source_get_oauth2_access_token_finish;

	g_object_class_install_property (
		object_class,
		PROP_DBUS_OBJECT,
		g_param_spec_object (
			"dbus-object",
			"D-Bus Object",
			"The D-Bus object for the data source",
			E_DBUS_TYPE_OBJECT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_DISPLAY_NAME,
		g_param_spec_string (
			"display-name",
			"Display Name",
			"The human-readable name of the data source",
			_("Unnamed"),
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_ENABLED,
		g_param_spec_boolean (
			"enabled",
			"Enabled",
			"Whether the data source is enabled",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_MAIN_CONTEXT,
		g_param_spec_boxed (
			"main-context",
			"Main Context",
			"The main loop context on "
			"which to attach event sources",
			G_TYPE_MAIN_CONTEXT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PARENT,
		g_param_spec_string (
			"parent",
			"Parent",
			"The unique identity of the parent data source",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_REMOTE_CREATABLE,
		g_param_spec_boolean (
			"remote-creatable",
			"Remote Creatable",
			"Whether the data source "
			"can create remote resources",
			FALSE,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_REMOTE_DELETABLE,
		g_param_spec_boolean (
			"remote-deletable",
			"Remote Deletable",
			"Whether the data source "
			"can delete remote resources",
			FALSE,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_REMOVABLE,
		g_param_spec_boolean (
			"removable",
			"Removable",
			"Whether the data source is removable",
			FALSE,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_UID,
		g_param_spec_string (
			"uid",
			"UID",
			"The unique identity of the data source",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_WRITABLE,
		g_param_spec_boolean (
			"writable",
			"Writable",
			"Whether the data source is writable",
			FALSE,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESource::changed:
	 * @source: the #ESource that received the signal
	 *
	 * The ::changed signal is emitted when a property in @source or
	 * one of its extension objects changes.  A common use for this
	 * signal is to notify a #GtkTreeModel containing data collected
	 * from #ESource<!-- -->s that it needs to update a row.
	 **/
	signals[CHANGED] = g_signal_new (
		"changed",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
		G_STRUCT_OFFSET (ESourceClass, changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/* Register built-in ESourceExtension types. */
	g_type_ensure (E_TYPE_SOURCE_ADDRESS_BOOK);
	g_type_ensure (E_TYPE_SOURCE_ALARMS);
	g_type_ensure (E_TYPE_SOURCE_AUTHENTICATION);
	g_type_ensure (E_TYPE_SOURCE_AUTOCOMPLETE);
	g_type_ensure (E_TYPE_SOURCE_CALENDAR);
	g_type_ensure (E_TYPE_SOURCE_COLLECTION);
	g_type_ensure (E_TYPE_SOURCE_GOA);
	g_type_ensure (E_TYPE_SOURCE_MAIL_ACCOUNT);
	g_type_ensure (E_TYPE_SOURCE_MAIL_COMPOSITION);
	g_type_ensure (E_TYPE_SOURCE_MAIL_IDENTITY);
	g_type_ensure (E_TYPE_SOURCE_MAIL_SIGNATURE);
	g_type_ensure (E_TYPE_SOURCE_MAIL_SUBMISSION);
	g_type_ensure (E_TYPE_SOURCE_MAIL_TRANSPORT);
	g_type_ensure (E_TYPE_SOURCE_MDN);
	g_type_ensure (E_TYPE_SOURCE_MEMO_LIST);
	g_type_ensure (E_TYPE_SOURCE_OFFLINE);
	g_type_ensure (E_TYPE_SOURCE_OPENPGP);
	g_type_ensure (E_TYPE_SOURCE_PROXY);
	g_type_ensure (E_TYPE_SOURCE_REFRESH);
	g_type_ensure (E_TYPE_SOURCE_RESOURCE);
	g_type_ensure (E_TYPE_SOURCE_REVISION_GUARDS);
	g_type_ensure (E_TYPE_SOURCE_SECURITY);
	g_type_ensure (E_TYPE_SOURCE_SELECTABLE);
	g_type_ensure (E_TYPE_SOURCE_SMIME);
	g_type_ensure (E_TYPE_SOURCE_TASK_LIST);
	g_type_ensure (E_TYPE_SOURCE_UOA);
	g_type_ensure (E_TYPE_SOURCE_WEBDAV);
}

static void
e_source_initable_init (GInitableIface *iface)
{
	iface->init = source_initable_init;
}

static void
e_source_proxy_resolver_init (GProxyResolverInterface *iface)
{
	iface->is_supported = source_proxy_resolver_is_supported;
	iface->lookup = source_proxy_resolver_lookup;
	iface->lookup_async = source_proxy_resolver_lookup_async;
	iface->lookup_finish = source_proxy_resolver_lookup_finish;
}

static void
e_source_init (ESource *source)
{
	GHashTable *extensions;

	/* Don't do this as part of class initialization because it
	 * loads Camel modules and can screw up introspection, which
	 * occurs at compile-time before Camel modules are installed. */
	e_source_camel_register_types ();

	extensions = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	source->priv = E_SOURCE_GET_PRIVATE (source);
	g_mutex_init (&source->priv->changed_lock);
	g_mutex_init (&source->priv->data_change_lock);
	g_mutex_init (&source->priv->property_lock);
	source->priv->key_file = g_key_file_new ();
	source->priv->extensions = extensions;

	g_rec_mutex_init (&source->priv->lock);
}

void
__e_source_private_replace_dbus_object (ESource *source,
                                        GDBusObject *dbus_object)
{
	/* XXX This function is only ever called by ESourceRegistry
	 *     either when the registry service reported an ESource
	 *     removal, or while recovering from a registry service
	 *     restart.  In the first case the GDBusObject is NULL,
	 *     in the second case the GDBusObject is an equivalent
	 *     proxy for the newly-started registry service. */

	g_return_if_fail (E_IS_SOURCE (source));

	if (dbus_object != NULL) {
		g_return_if_fail (E_DBUS_IS_OBJECT (dbus_object));
		dbus_object = g_object_ref (dbus_object);
	}

	g_mutex_lock (&source->priv->property_lock);

	g_clear_object (&source->priv->dbus_object);
	source->priv->dbus_object = dbus_object;

	g_mutex_unlock (&source->priv->property_lock);

	g_object_notify (G_OBJECT (source), "dbus-object");
}

/**
 * e_source_new:
 * @dbus_object: (allow-none): a #GDBusObject or %NULL
 * @main_context: (allow-none): a #GMainContext or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #ESource instance.
 *
 * The #ESource::changed signal will be emitted from @main_context if given,
 * or else from the thread-default #GMainContext at the time this function is
 * called.
 *
 * The only time the function should be called outside of #ESourceRegistry
 * is to create a so-called "scratch" #ESource for editing in a Properties
 * window or an account setup assistant.
 *
 * FIXME: Elaborate on scratch sources.
 *
 * Returns: a new #ESource, or %NULL on error
 *
 * Since: 3.6
 **/
ESource *
e_source_new (GDBusObject *dbus_object,
              GMainContext *main_context,
              GError **error)
{
	if (dbus_object != NULL)
		g_return_val_if_fail (E_DBUS_IS_OBJECT (dbus_object), NULL);

	return g_initable_new (
		E_TYPE_SOURCE, NULL, error,
		"dbus-object", dbus_object,
		"main-context", main_context,
		NULL);
}

/**
 * e_source_new_with_uid:
 * @uid: a new unique identifier string
 * @main_context: (allow-none): a #GMainContext or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new "scratch" #ESource with a predetermined unique identifier.
 *
 * The #ESource::changed signal will be emitted from @main_context if given,
 * or else from the thread-default #GMainContext at the time this function is
 * called.
 *
 * Returns: a new scratch #ESource, or %NULL on error
 *
 * Since: 3.6
 **/
ESource *
e_source_new_with_uid (const gchar *uid,
                       GMainContext *main_context,
                       GError **error)
{
	g_return_val_if_fail (uid != NULL, NULL);

	return g_initable_new (
		E_TYPE_SOURCE, NULL, error,
		"main-context", main_context,
		"uid", uid, NULL);
}

/**
 * e_source_hash:
 * @source: an #ESource
 *
 * Generates a hash value for @source.  This function is intended for
 * easily hashing an #ESource to add to a #GHashTable or similar data
 * structure.
 *
 * Returns: a hash value for @source.
 *
 * Since: 3.6
 **/
guint
e_source_hash (ESource *source)
{
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE (source), 0);

	uid = e_source_get_uid (source);

	return g_str_hash (uid);
}

/**
 * e_source_equal:
 * @source1: the first #ESource
 * @source2: the second #ESource
 *
 * Checks two #ESource instances for equality.  #ESource instances are
 * equal if their unique identifier strings are equal.
 *
 * Returns: %TRUE if @source1 and @source2 are equal
 *
 * Since: 3.6
 **/
gboolean
e_source_equal (ESource *source1,
                ESource *source2)
{
	const gchar *uid1, *uid2;

	g_return_val_if_fail (E_IS_SOURCE (source1), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source2), FALSE);

	if (source1 == source2)
		return TRUE;

	uid1 = e_source_get_uid (source1);
	uid2 = e_source_get_uid (source2);

	return g_str_equal (uid1, uid2);
}

/**
 * e_source_changed:
 * @source: an #ESource
 *
 * Emits the #ESource::changed signal from an idle callback in
 * @source's #ESource:main-context.
 *
 * This function is primarily intended for use by #ESourceExtension
 * when emitting a #GObject::notify signal on one of its properties.
 *
 * Since: 3.6
 **/
void
e_source_changed (ESource *source)
{
	g_return_if_fail (E_IS_SOURCE (source));

	g_mutex_lock (&source->priv->changed_lock);
	if (source->priv->changed == NULL) {
		source->priv->changed = g_idle_source_new ();
		g_source_set_callback (
			source->priv->changed,
			source_idle_changed_cb,
			source, (GDestroyNotify) NULL);
		g_source_attach (
			source->priv->changed,
			source->priv->main_context);
	}
	g_mutex_unlock (&source->priv->changed_lock);
}

/**
 * e_source_get_uid:
 * @source: an #ESource
 *
 * Returns the unique identifier string for @source.
 *
 * Returns: the UID for @source
 *
 * Since: 3.6
 **/
const gchar *
e_source_get_uid (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->uid;
}

/**
 * e_source_dup_uid:
 * @source: an #ESource
 *
 * Thread-safe variation of e_source_get_uid().
 * Use this function when accessing @source from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESource:uid
 *
 * Since: 3.6
 **/
gchar *
e_source_dup_uid (ESource *source)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	/* Perhaps we don't need to lock the mutex since
	 * this is a read-only property but it can't hurt. */

	g_mutex_lock (&source->priv->property_lock);

	protected = e_source_get_uid (source);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&source->priv->property_lock);

	return duplicate;
}

/**
 * e_source_get_parent:
 * @source: an #ESource
 *
 * Returns the unique identifier string of the parent #ESource.
 *
 * Returns: the UID of the parent #ESource
 *
 * Since: 3.6
 **/
const gchar *
e_source_get_parent (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->parent;
}

/**
 * e_source_dup_parent:
 * @source: an #ESource
 *
 * Thread-safe variation of e_source_get_parent().
 * Use this function when accessing @source from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESource:parent
 *
 * Since: 3.6
 **/
gchar *
e_source_dup_parent (ESource *source)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	g_mutex_lock (&source->priv->property_lock);

	protected = e_source_get_parent (source);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&source->priv->property_lock);

	return duplicate;
}

/**
 * e_source_set_parent:
 * @source: an #ESource
 * @parent: (allow-none): the UID of the parent #ESource, or %NULL
 *
 * Identifies the parent of @source by its unique identifier string.
 * This can only be set prior to adding @source to an #ESourceRegistry.
 *
 * The internal copy of #ESource:parent is automatically stripped of leading
 * and trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.6
 **/
void
e_source_set_parent (ESource *source,
                     const gchar *parent)
{
	g_return_if_fail (E_IS_SOURCE (source));

	g_mutex_lock (&source->priv->property_lock);

	if (g_strcmp0 (source->priv->parent, parent) == 0) {
		g_mutex_unlock (&source->priv->property_lock);
		return;
	}

	g_free (source->priv->parent);
	source->priv->parent = e_util_strdup_strip (parent);

	g_mutex_unlock (&source->priv->property_lock);

	g_object_notify (G_OBJECT (source), "parent");
}

/**
 * e_source_get_enabled:
 * @source: an #ESource
 *
 * Returns %TRUE if @source is enabled.
 *
 * An application should try to honor this setting if at all possible,
 * even if it does not provide a way to change the setting through its
 * user interface.  Disabled data sources should generally be hidden.
 *
 * <note><para>
 *   This function does not take into account @source's ancestors in the
 *   #ESource hierarchy, each of which have their own enabled state.  If
 *   any of @source's ancestors are disabled, then @source itself should
 *   be treated as disabled.  Use e_source_registry_check_enabled() to
 *   easily check for this.
 * </para></note>
 *
 * Returns: whether @source is enabled
 *
 * Since: 3.6
 **/
gboolean
e_source_get_enabled (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	return source->priv->enabled;
}

/**
 * e_source_set_enabled:
 * @source: an #ESource
 * @enabled: whether to enable @source
 *
 * Enables or disables @source.
 *
 * An application should try to honor this setting if at all possible,
 * even if it does not provide a way to change the setting through its
 * user interface.  Disabled data sources should generally be hidden.
 *
 * Since: 3.6
 **/
void
e_source_set_enabled (ESource *source,
                      gboolean enabled)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (source->priv->enabled == enabled)
		return;

	source->priv->enabled = enabled;

	g_object_notify (G_OBJECT (source), "enabled");
}

/**
 * e_source_get_writable:
 * @source: an #ESource
 *
 * Returns whether the D-Bus service will accept changes to @source.
 * If @source is not writable, calls to e_source_write() will fail.
 *
 * Returns: whether @source is writable
 *
 * Since: 3.6
 **/
gboolean
e_source_get_writable (ESource *source)
{
	GDBusObject *dbus_object;
	gboolean writable = FALSE;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		EDBusSourceWritable *dbus_interface;

		dbus_interface =
			e_dbus_object_peek_source_writable (
			E_DBUS_OBJECT (dbus_object));
		writable = (dbus_interface != NULL);
		g_object_unref (dbus_object);
	}

	return writable;
}

/**
 * e_source_get_removable:
 * @source: an #ESource
 *
 * Returns whether the D-Bus service will allow @source to be removed.
 * If @source is not writable, calls to e_source_remove() will fail.
 *
 * Returns: whether @source is removable
 *
 * Since: 3.6
 **/
gboolean
e_source_get_removable (ESource *source)
{
	GDBusObject *dbus_object;
	gboolean removable = FALSE;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		EDBusSourceRemovable *dbus_interface;

		dbus_interface =
			e_dbus_object_peek_source_removable (
			E_DBUS_OBJECT (dbus_object));
		removable = (dbus_interface != NULL);
		g_object_unref (dbus_object);
	}

	return removable;
}

/**
 * e_source_get_remote_creatable:
 * @source: an #ESource
 *
 * Returns whether new resources can be created on a remote server by
 * calling e_source_remote_create() on @source.
 *
 * Generally this is only %TRUE if @source has an #ESourceCollection
 * extension, which means there is an #ECollectionBackend in the D-Bus
 * service that can handle create requests.  If @source does not have
 * this capability, calls to e_source_remote_create() will fail.
 *
 * Returns: whether @source can create remote resources
 *
 * Since: 3.6
 **/
gboolean
e_source_get_remote_creatable (ESource *source)
{
	GDBusObject *dbus_object;
	gboolean remote_creatable = FALSE;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		EDBusSourceRemoteCreatable *dbus_interface;

		dbus_interface =
			e_dbus_object_peek_source_remote_creatable (
			E_DBUS_OBJECT (dbus_object));
		remote_creatable = (dbus_interface != NULL);
		g_object_unref (dbus_object);
	}

	return remote_creatable;
}

/**
 * e_source_get_remote_deletable:
 * @source: an #ESource
 *
 * Returns whether the resource represented by @source can be deleted
 * from a remote server by calling e_source_remote_delete().
 *
 * Generally this is only %TRUE if @source is a child of an #ESource
 * which has an #ESourceCollection extension, which means there is an
 * #ECollectionBackend in the D-Bus service that can handle delete
 * requests.  If @source does not have this capability, calls to
 * e_source_remote_delete() will fail.
 *
 * Returns: whether @source can delete remote resources
 *
 * Since: 3.6
 **/
gboolean
e_source_get_remote_deletable (ESource *source)
{
	GDBusObject *dbus_object;
	gboolean remote_deletable = FALSE;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		EDBusSourceRemoteDeletable *dbus_interface;

		dbus_interface =
			e_dbus_object_peek_source_remote_deletable (
			E_DBUS_OBJECT (dbus_object));
		remote_deletable = (dbus_interface != NULL);
		g_object_unref (dbus_object);
	}

	return remote_deletable;
}

/**
 * e_source_get_extension:
 * @source: an #ESource
 * @extension_name: an extension name
 *
 * Returns an instance of some #ESourceExtension subclass which registered
 * itself under @extension_name.  If no such instance exists within @source,
 * one will be created.  It is the caller's responsibility to know which
 * subclass is being returned.
 *
 * If you just want to test for the existence of an extension within @source
 * without creating it, use e_source_has_extension().
 *
 * Extension instances are owned by their #ESource and should not be
 * referenced directly.  Instead, reference the #ESource instance and
 * use this function to fetch the extension instance as needed.
 *
 * Returns: (type ESourceExtension) (transfer none): an instance of some
 * #ESourceExtension subclass
 *
 * Since: 3.6
 **/
gpointer
e_source_get_extension (ESource *source,
                        const gchar *extension_name)
{
	ESourceExtension *extension;
	GHashTable *hash_table;
	GTypeClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (extension_name != NULL, NULL);

	g_rec_mutex_lock (&source->priv->lock);

	/* Check if we already have the extension. */
	extension = g_hash_table_lookup (
		source->priv->extensions, extension_name);
	if (extension != NULL)
		goto exit;

	/* Find all subclasses of ESourceExtensionClass. */
	hash_table = source_find_extension_classes ();
	class = g_hash_table_lookup (hash_table, extension_name);

	/* Create a new instance of the appropriate GType. */
	if (class != NULL) {
		extension = g_object_new (
			G_TYPE_FROM_CLASS (class),
			"source", source, NULL);
		source_load_from_key_file (
			G_OBJECT (extension),
			source->priv->key_file,
			extension_name);
		g_hash_table_insert (
			source->priv->extensions,
			g_strdup (extension_name), extension);
	} else {
		/* XXX Tie this into a debug setting for ESources. */
#ifdef DEBUG
		g_critical (
			"No registered GType for ESource "
			"extension '%s'", extension_name);
#endif
	}

	g_hash_table_destroy (hash_table);

exit:
	g_rec_mutex_unlock (&source->priv->lock);

	return extension;
}

/**
 * e_source_has_extension:
 * @source: an #ESource
 * @extension_name: an extension name
 *
 * Checks whether @source has an #ESourceExtension with the given name.
 *
 * Returns: %TRUE if @source has such an extension, %FALSE if not
 *
 * Since: 3.6
 **/
gboolean
e_source_has_extension (ESource *source,
                        const gchar *extension_name)
{
	ESourceExtension *extension;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (extension_name != NULL, FALSE);

	g_rec_mutex_lock (&source->priv->lock);

	/* Two cases to check for, either one is good enough:
	 * 1) Our internal GKeyFile has a group named 'extension_name'.
	 * 2) Our 'extensions' table has an entry for 'extension_name'.
	 *
	 * We have to check both data structures in case a new extension
	 * not present in the GKeyFile was instantiated, but we have not
	 * yet updated our internal GKeyFile.  A common occurrence when
	 * editing a brand new data source.
	 *
	 * When checking the GKeyFile we want to actually fetch the
	 * extension with e_source_get_extension() to make sure it's
	 * a registered extension name and not just an arbitrary key
	 * file group name. */

	if (g_key_file_has_group (source->priv->key_file, extension_name)) {
		extension = e_source_get_extension (source, extension_name);
	} else {
		GHashTable *hash_table = source->priv->extensions;
		extension = g_hash_table_lookup (hash_table, extension_name);
	}

	g_rec_mutex_unlock (&source->priv->lock);

	return (extension != NULL);
}

/**
 * e_source_ref_dbus_object:
 * @source: an #ESource
 *
 * Returns the #GDBusObject that was passed to e_source_new().
 *
 * The returned #GDBusObject is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the #GDBusObject for @source, or %NULL
 *
 * Since: 3.6
 **/
GDBusObject *
e_source_ref_dbus_object (ESource *source)
{
	GDBusObject *dbus_object = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	g_mutex_lock (&source->priv->property_lock);

	if (source->priv->dbus_object != NULL)
		dbus_object = g_object_ref (source->priv->dbus_object);

	g_mutex_unlock (&source->priv->property_lock);

	return dbus_object;
}

/**
 * e_source_ref_main_context:
 * @source: an #ESource
 *
 * Returns the #GMainContext on which event sources for @source are to
 * be attached.
 *
 * The returned #GMainContext is referenced for thread-safety and must be
 * unreferenced with g_main_context_unref() when finished with it.
 *
 * Returns: (transfer full): a #GMainContext
 *
 * Since: 3.6
 **/
GMainContext *
e_source_ref_main_context (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return g_main_context_ref (source->priv->main_context);
}

/**
 * e_source_get_display_name:
 * @source: an #ESource
 *
 * Returns the display name for @source.  Use the display name to
 * represent the #ESource in a user interface.
 *
 * Returns: the display name for @source
 *
 * Since: 3.6
 **/
const gchar *
e_source_get_display_name (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->display_name;
}

/**
 * e_source_dup_display_name:
 * @source: an #ESource
 *
 * Thread-safe variation of e_source_get_display_name().
 * Use this function when accessing @source from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESource:display-name
 *
 * Since: 3.6
 **/
gchar *
e_source_dup_display_name (ESource *source)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	g_mutex_lock (&source->priv->property_lock);

	protected = e_source_get_display_name (source);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&source->priv->property_lock);

	return duplicate;
}

/**
 * e_source_set_display_name:
 * @source: an #ESource
 * @display_name: a display name
 *
 * Sets the display name for @source.  The @display_name argument must be a
 * valid UTF-8 string.  Use the display name to represent the #ESource in a
 * user interface.
 *
 * The internal copy of @display_name is automatically stripped of leading
 * and trailing whitespace.
 *
 * Since: 3.6
 **/
void
e_source_set_display_name (ESource *source,
                           const gchar *display_name)
{
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (display_name != NULL);
	g_return_if_fail (g_utf8_validate (display_name, -1, NULL));

	g_mutex_lock (&source->priv->property_lock);

	if (g_strcmp0 (source->priv->display_name, display_name) == 0) {
		g_mutex_unlock (&source->priv->property_lock);
		return;
	}

	g_free (source->priv->display_name);
	source->priv->display_name = g_strdup (display_name);

	/* Strip leading and trailing whitespace. */
	g_strstrip (source->priv->display_name);

	/* This is used in e_source_compare_by_display_name(). */
	g_free (source->priv->collate_key);
	source->priv->collate_key = g_utf8_collate_key (display_name, -1);

	g_mutex_unlock (&source->priv->property_lock);

	g_object_notify (G_OBJECT (source), "display-name");
}

/**
 * e_source_dup_secret_label:
 * @source: an #ESource
 *
 * Creates a label string based on @source's #ESource:display-name for use
 * with #SecretItem.
 *
 * Returns: a newly-allocated secret label
 *
 * Since: 3.12
 **/
gchar *
e_source_dup_secret_label (ESource *source)
{
	gchar *display_name;
	gchar *secret_label;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	display_name = e_source_dup_display_name (source);

	if (display_name == NULL || *display_name == '\0') {
		g_free (display_name);
		display_name = e_source_dup_uid (source);
	}

	secret_label = g_strdup_printf (
		"Evolution Data Source \"%s\"", display_name);

	g_free (display_name);

	return secret_label;
}

/**
 * e_source_compare_by_display_name:
 * @source1: the first #ESource
 * @source2: the second #ESource
 *
 * Compares two #ESource instances by their display names.  Useful for
 * ordering sources in a user interface.
 *
 * Returns: a negative value if @source1 compares before @source2, zero if
 *          they compare equal, or a positive value if @source1 compares
 *          after @source2
 *
 * Since: 3.6
 **/
gint
e_source_compare_by_display_name (ESource *source1,
                                  ESource *source2)
{
	return g_strcmp0 (
		source1->priv->collate_key,
		source2->priv->collate_key);
}

/**
 * e_source_to_string:
 * @source: an #ESource
 * @length: (allow-none): return location for the length of the returned
 *          string, or %NULL
 *
 * Outputs the current contents of @source as a key file string.
 * Free the returned string with g_free().
 *
 * Returns: a newly-allocated string
 *
 * Since: 3.6
 **/
gchar *
e_source_to_string (ESource *source,
                    gsize *length)
{
	GHashTableIter iter;
	GKeyFile *key_file;
	gpointer group_name;
	gpointer extension;
	gchar *data;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	g_rec_mutex_lock (&source->priv->lock);

	key_file = source->priv->key_file;

	source_save_to_key_file (
		G_OBJECT (source), key_file, PRIMARY_GROUP_NAME);

	g_hash_table_iter_init (&iter, source->priv->extensions);
	while (g_hash_table_iter_next (&iter, &group_name, &extension))
		source_save_to_key_file (extension, key_file, group_name);

	data = g_key_file_to_data (key_file, length, NULL);

	g_rec_mutex_unlock (&source->priv->lock);

	return data;
}

/**
 * e_source_parameter_to_key:
 * @param_name: a #GParamSpec name
 *
 * Converts a #GParamSpec name (e.g. "foo-bar" or "foo_bar")
 * to "CamelCase" for use as a #GKeyFile key (e.g. "FooBar").
 *
 * This function is made public only to aid in account migration.
 * Applications should not need to use this.
 *
 * Since: 3.6
 **/
gchar *
e_source_parameter_to_key (const gchar *param_name)
{
	gboolean uppercase = TRUE;
	gchar *key, *cp;
	gint ii;

	g_return_val_if_fail (param_name != NULL, NULL);

	key = cp = g_malloc0 (strlen (param_name) + 1);

	for (ii = 0; param_name[ii] != '\0'; ii++) {
		if (g_ascii_isalnum (param_name[ii]) && uppercase) {
			*cp++ = g_ascii_toupper (param_name[ii]);
			uppercase = FALSE;
		} else if (param_name[ii] == '-' || param_name[ii] == '_')
			uppercase = TRUE;
		else
			*cp++ = param_name[ii];
	}

	return key;
}

/**
 * e_source_remove_sync:
 * @source: the #ESource to be removed
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Requests the D-Bus service to delete the key files for @source and all of
 * its descendants and broadcast their removal to all clients.  The @source
 * must be #ESource:removable.
 *
 * If an error occurs, the functon will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 **/
gboolean
e_source_remove_sync (ESource *source,
                      GCancellable *cancellable,
                      GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->remove_sync != NULL, FALSE);

	return class->remove_sync (source, cancellable, error);
}

/**
 * e_source_remove:
 * @source: the #ESource to be removed
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously requests the D-Bus service to delete the key files for
 * @source and all of its descendants and broadcast their removal to all
 * clients.  The @source must be #ESource:removable.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_remove_finish() to get the result of the operation.
 *
 * Since: 3.6
 **/
void
e_source_remove (ESource *source,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
	ESourceClass *class;

	g_return_if_fail (E_IS_SOURCE (source));

	class = E_SOURCE_GET_CLASS (source);
	g_return_if_fail (class->remove != NULL);

	class->remove (source, cancellable, callback, user_data);
}

/**
 * e_source_remove_finish:
 * @source: the #ESource to be removed
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_remove().  If an
 * error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE of failure
 *
 * Since: 3.6
 **/
gboolean
e_source_remove_finish (ESource *source,
                        GAsyncResult *result,
                        GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->remove_finish != NULL, FALSE);

	return class->remove_finish (source, result, error);
}

/**
 * e_source_write_sync:
 * @source: a writable #ESource
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Submits the current contents of @source to the D-Bus service to be
 * written to disk and broadcast to other clients.  The @source must
 * be #ESource:writable.
 *
 * If an error occurs, the functon will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 **/
gboolean
e_source_write_sync (ESource *source,
                     GCancellable *cancellable,
                     GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->write_sync != NULL, FALSE);

	return class->write_sync (source, cancellable, error);
}

/**
 * e_source_write:
 * @source: a writable #ESource
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously submits the current contents of @source to the D-Bus
 * service to be written to disk and broadcast to other clients.  The
 * @source must be #ESource:writable.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_write_finish() to get the result of the operation.
 *
 * Since: 3.6
 **/
void
e_source_write (ESource *source,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
	ESourceClass *class;

	g_return_if_fail (E_IS_SOURCE (source));

	class = E_SOURCE_GET_CLASS (source);
	g_return_if_fail (class->write != NULL);

	class->write (source, cancellable, callback, user_data);
}

/**
 * e_source_write_finish:
 * @source: a writable #ESource
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_write().  If an
 * error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 **/
gboolean
e_source_write_finish (ESource *source,
                       GAsyncResult *result,
                       GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->write_finish != NULL, FALSE);

	return class->write_finish (source, result, error);
}

/**
 * e_source_remote_create_sync:
 * @source: an #ESource
 * @scratch_source: an #ESource describing the resource to create
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new remote resource by picking out relevant details from
 * @scratch_source.  The @scratch_source must be an #ESource with no
 * #GDBusObject.  The @source must be #ESource:remote-creatable.
 *
 * The details required to create the resource vary by #ECollectionBackend,
 * but in most cases the @scratch_source need only define the resource type
 * (address book, calendar, etc.), a display name for the resource, and
 * possibly a server-side path or ID for the resource.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 **/
gboolean
e_source_remote_create_sync (ESource *source,
                             ESource *scratch_source,
                             GCancellable *cancellable,
                             GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (scratch_source), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->remote_create_sync != NULL, FALSE);

	return class->remote_create_sync (
		source, scratch_source, cancellable, error);
}

/**
 * e_source_remote_create:
 * @source: an #ESource
 * @scratch_source: an #ESource describing the resource to create
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously creates a new remote resource by picking out relevant
 * details from @scratch_source.  The @scratch_source must be an #ESource
 * with no #GDBusObject.  The @source must be #ESource:remote-creatable.
 *
 * The details required to create the resource vary by #ECollectionBackend,
 * but in most cases the @scratch_source need only define the resource type
 * (address book, calendar, etc.), a display name for the resource, and
 * possibly a server-side path or ID for the resource.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_remote_create_finish() to get the result of the operation.
 *
 * Since: 3.6
 **/
void
e_source_remote_create (ESource *source,
                        ESource *scratch_source,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	ESourceClass *class;

	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (E_IS_SOURCE (scratch_source));

	class = E_SOURCE_GET_CLASS (source);
	g_return_if_fail (class->remote_create != NULL);

	class->remote_create (
		source, scratch_source,
		cancellable, callback, user_data);
}

/**
 * e_source_remote_create_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_remote_create().  If
 * an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 **/
gboolean
e_source_remote_create_finish (ESource *source,
                               GAsyncResult *result,
                               GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->remote_create_finish != NULL, FALSE);

	return class->remote_create_finish (source, result, error);
}

/**
 * e_source_remote_delete_sync:
 * @source: an #ESource
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes the resource represented by @source from a remote server.
 * The @source must be #ESource:remote-deletable.  This will also delete
 * the key file for @source and broadcast its removal to all clients,
 * similar to e_source_remove_sync().
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 **/
gboolean
e_source_remote_delete_sync (ESource *source,
                             GCancellable *cancellable,
                             GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->remote_delete_sync != NULL, FALSE);

	return class->remote_delete_sync (source, cancellable, error);
}

/**
 * e_source_remote_delete:
 * @source: an #ESource
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously deletes the resource represented by @source from a remote
 * server.  The @source must be #ESource:remote-deletable.  This will also
 * delete the key file for @source and broadcast its removal to all clients,
 * similar to e_source_remove().
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_remote_delete_finish() to get the result of the operation.
 *
 * Since: 3.6
 **/
void
e_source_remote_delete (ESource *source,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	ESourceClass *class;

	g_return_if_fail (E_IS_SOURCE (source));

	class = E_SOURCE_GET_CLASS (source);
	g_return_if_fail (class->remote_delete != NULL);

	class->remote_delete (source, cancellable, callback, user_data);
}

/**
 * e_source_remote_delete_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_remote_delete().  If
 * an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 **/
gboolean
e_source_remote_delete_finish (ESource *source,
                               GAsyncResult *result,
                               GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (class->remote_delete_finish != NULL, FALSE);

	return class->remote_delete_finish (source, result, error);
}

/**
 * e_source_get_oauth2_access_token_sync:
 * @source: an #ESource
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @out_access_token: (allow-none) (out): return location for the access token,
 *                    or %NULL
 * @out_expires_in: (allow-none) (out): return location for the token expiry,
 *                  or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains the OAuth 2.0 access token for @source along with its expiry
 * in seconds from the current time (or 0 if unknown).
 *
 * Free the returned access token with g_free() when finished with it.
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.8
 **/
gboolean
e_source_get_oauth2_access_token_sync (ESource *source,
                                       GCancellable *cancellable,
                                       gchar **out_access_token,
                                       gint *out_expires_in,
                                       GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (
		class->get_oauth2_access_token_sync != NULL, FALSE);

	return class->get_oauth2_access_token_sync (
		source, cancellable, out_access_token, out_expires_in, error);
}

/**
 * e_source_get_oauth2_access_token:
 * @source: an #ESource
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously obtains the OAuth 2.0 access token for @source along
 * with its expiry in seconds from the current time (or 0 if unknown).
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_get_oauth2_access_token_finish() to get the result of the
 * operation.
 *
 * Since: 3.8
 **/
void
e_source_get_oauth2_access_token (ESource *source,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	ESourceClass *class;

	g_return_if_fail (E_IS_SOURCE (source));

	class = E_SOURCE_GET_CLASS (source);
	g_return_if_fail (class->get_oauth2_access_token != NULL);

	return class->get_oauth2_access_token (
		source, cancellable, callback, user_data);
}

/**
 * e_source_get_oauth2_access_token_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @out_access_token: (allow-none) (out): return location for the access token,
 *                    or %NULL
 * @out_expires_in: (allow-none) (out): return location for the token expiry,
 *                  or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_get_oauth2_access_token().
 *
 * Free the returned access token with g_free() when finished with it.
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.8
 **/
gboolean
e_source_get_oauth2_access_token_finish (ESource *source,
                                         GAsyncResult *result,
                                         gchar **out_access_token,
                                         gint *out_expires_in,
                                         GError **error)
{
	ESourceClass *class;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = E_SOURCE_GET_CLASS (source);
	g_return_val_if_fail (
		class->get_oauth2_access_token_finish != NULL, FALSE);

	return class->get_oauth2_access_token_finish (
		source, result, out_access_token, out_expires_in, error);
}

/**
 * e_source_store_password_sync:
 * @source: an #ESource
 * @password: the password to store
 * @permanently: store permanently or just for the session
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Stores a password for @source.  This operation does not rely on the
 * registry service and therefore works for any #ESource -- registered
 * or "scratch".
 *
 * If @permanently is %TRUE, the password is stored in the default keyring.
 * Otherwise the password is stored in the memory-only session keyring.  If
 * an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.12
 **/
gboolean
e_source_store_password_sync (ESource *source,
                              const gchar *password,
                              gboolean permanently,
                              GCancellable *cancellable,
                              GError **error)
{
	gboolean success;
#ifndef G_OS_WIN32
	const gchar *collection;
#endif
	const gchar *uid;
	gchar *label;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (password != NULL, FALSE);

#ifndef G_OS_WIN32
	if (permanently)
		collection = SECRET_COLLECTION_DEFAULT;
	else
		collection = SECRET_COLLECTION_SESSION;
#endif

	uid = e_source_get_uid (source);
	label = e_source_dup_secret_label (source);

#ifdef G_OS_WIN32
	success = e_win32_source_store_password_sync (uid, password, error);
#else
	success = secret_password_store_sync (
		&password_schema,
		collection, label, password,
		cancellable, error,
		KEYRING_ITEM_ATTRIBUTE_NAME, uid,
		NULL);
#endif

	g_free (label);

	return success;
}

/* Helper for e_source_store_password() */
static void
source_store_password_thread (GTask *task,
                              gpointer source_object,
                              gpointer task_data,
                              GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = e_source_store_password_sync (
		E_SOURCE (source_object),
		async_context->password,
		async_context->permanently,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * e_source_store_password:
 * @source: an #ESource
 * @password: the password to store
 * @permanently: store permanently or just for the session
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously stores a password for @source.  This operation does
 * not rely on the registry service and therefore works for any #ESource
 * -- registered or "scratch".
 *
 * If @permanently is %TRUE, the password is stored in the default keyring.
 * Otherwise the password is stored in the memory-only session keyring.  If
 * an error occurs, the function sets @error and returns %FALSE.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_store_password_finish() to get the result of the operation.
 *
 * Since: 3.12
 **/
void
e_source_store_password (ESource *source,
                         const gchar *password,
                         gboolean permanently,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (password != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->password = g_strdup (password);
	async_context->permanently = permanently;

	task = g_task_new (source, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_source_store_password);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, source_store_password_thread);

	g_object_unref (task);
}

/**
 * e_source_store_password_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_store_password().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.12
 **/
gboolean
e_source_store_password_finish (ESource *source,
                                GAsyncResult *result,
                                GError **error)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, source), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, e_source_store_password), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_source_lookup_password_sync:
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @out_password: (out): return location for the password, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Looks up a password for @source.  Both the default and session keyrings
 * are queried.  This operation does not rely on the registry service and
 * therefore works for any #ESource -- registered or "scratch".
 *
 * Note the boolean return value indicates whether the lookup operation
 * itself completed successfully, not whether a password was found.  If
 * no password was found, the function will set @out_password to %NULL
 * but still return %TRUE.  If an error occurs, the function sets @error
 * and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.12
 **/
gboolean
e_source_lookup_password_sync (ESource *source,
                               GCancellable *cancellable,
                               gchar **out_password,
                               GError **error)
{
	const gchar *uid;
	gchar *temp = NULL;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	uid = e_source_get_uid (source);

#ifdef G_OS_WIN32
	temp = e_win32_source_lookup_password_sync (uid, &local_error);
#else
	temp = secret_password_lookup_sync (
		&password_schema,
		cancellable, &local_error,
		KEYRING_ITEM_ATTRIBUTE_NAME, uid,
		NULL);
#endif

	if (local_error != NULL) {
		g_warn_if_fail (temp == NULL);
		g_propagate_error (error, local_error);
		success = FALSE;
	} else if (out_password != NULL) {
		*out_password = temp;  /* takes ownership */
	} else {
		secret_password_free (temp);
	}

	return success;
}

/* Helper for e_source_lookup_password() */
static void
source_lookup_password_thread (GTask *task,
                               gpointer source_object,
                               gpointer task_data,
                               GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = e_source_lookup_password_sync (
		E_SOURCE (source_object),
		cancellable,
		&async_context->password,
		&local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * e_source_lookup_password:
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously looks up a password for @source.  Both the default and
 * session keyrings are queried.  This operation does not rely on the
 * registry service and therefore works for any #ESource -- registered
 * or "scratch".
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_lookup_password_finish() to get the result of the operation.
 *
 * Since: 3.12
 **/
void
e_source_lookup_password (ESource *source,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_SOURCE (source));

	async_context = g_slice_new0 (AsyncContext);

	task = g_task_new (source, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_source_lookup_password);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, source_lookup_password_thread);

	g_object_unref (task);
}

/**
 * e_source_lookup_password_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @out_password: (out): return location for the password, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_lookup_password().
 *
 * Note the boolean return value indicates whether the lookup operation
 * itself completed successfully, not whether a password was found.  If
 * no password was found, the function will set @out_password to %NULL
 * but still return %TRUE.  If an error occurs, the function sets @error
 * and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.12
 **/
gboolean
e_source_lookup_password_finish (ESource *source,
                                 GAsyncResult *result,
                                 gchar **out_password,
                                 GError **error)
{
	AsyncContext *async_context;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, source), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, e_source_lookup_password), FALSE);

	async_context = g_task_get_task_data (G_TASK (result));

	if (!g_task_had_error (G_TASK (result))) {
		if (out_password != NULL) {
			*out_password = async_context->password;
			async_context->password = NULL;
		}
	}

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_source_delete_password_sync:
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes the password for @source from either the default keyring or
 * session keyring.  This operation does not rely on the registry service
 * and therefore works for any #ESource -- registered or "scratch".
 *
 * Note the boolean return value indicates whether the delete operation
 * itself completed successfully, not whether a password was found and
 * deleted.  If no password was found, the function will still return
 * %TRUE.  If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.12
 **/
gboolean
e_source_delete_password_sync (ESource *source,
                               GCancellable *cancellable,
                               GError **error)
{
	const gchar *uid;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	uid = e_source_get_uid (source);

#ifdef G_OS_WIN32
	e_win32_source_delete_password_sync (uid, &local_error);
#else
	/* The return value indicates whether any passwords were removed,
	 * not whether the operation completed successfully.  So we have
	 * to check the GError directly. */
	secret_password_clear_sync (
		&password_schema,
		cancellable, &local_error,
		KEYRING_ITEM_ATTRIBUTE_NAME, uid,
		NULL);
#endif

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	return success;
}

/* Helper for e_source_delete_password() */
static void
source_delete_password_thread (GTask *task,
                               gpointer source_object,
                               gpointer task_data,
                               GCancellable *cancellable)
{
	gboolean success;
	GError *local_error = NULL;

	success = e_source_delete_password_sync (
		E_SOURCE (source_object),
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * e_source_delete_password:
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously deletes the password for @source from either the default
 * keyring or session keyring.  This operation does not rely on the registry
 * service and therefore works for any #ESource -- registered or "scratch".
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_delete_password_finish() to get the result of the operation.
 *
 * Since: 3.12
 **/
void
e_source_delete_password (ESource *source,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_SOURCE (source));

	task = g_task_new (source, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_source_delete_password);

	g_task_run_in_thread (task, source_delete_password_thread);

	g_object_unref (task);
}

/**
 * e_source_delete_password_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_delete_password().
 *
 * Note the boolean return value indicates whether the delete operation
 * itself completed successfully, not whether a password was found and
 * deleted.  If no password was found, the function will still return
 * %TRUE.  If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.12
 **/
gboolean
e_source_delete_password_finish (ESource *source,
                                 GAsyncResult *result,
                                 GError **error)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, source), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, e_source_delete_password), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_source_allow_auth_prompt_sync:
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Tells the source registry that it can ask for passwords, if necessary.
 * Password prompts are disabled automatically when a user cancels
 * the password prompt. This function is to reverse the effect. It does
 * nothing, if the password prompt is not disabled.
 *
 * If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.14
 **/
gboolean
e_source_allow_auth_prompt_sync (ESource *source,
				 GCancellable *cancellable,
				 GError **error)
{
	GDBusObject *dbus_object;
	EDBusSource *dbus_source = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	dbus_object = e_source_ref_dbus_object (source);
	if (dbus_object != NULL) {
		dbus_source = e_dbus_object_get_source (E_DBUS_OBJECT (dbus_object));
		g_object_unref (dbus_object);
	}

	if (!dbus_source) {
		g_warn_if_fail (dbus_source != NULL);
		return FALSE;
	}

	e_dbus_source_call_allow_auth_prompt_sync (dbus_source, cancellable, &local_error);

	g_object_unref (dbus_source);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static void
source_allow_auth_prompt_thread (GTask *task,
				 gpointer source_object,
				 gpointer task_data,
				 GCancellable *cancellable)
{
	gboolean success;
	GError *local_error = NULL;

	success = e_source_allow_auth_prompt_sync (
		E_SOURCE (source_object),
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * e_source_allow_auth_prompt:
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously tells the source registry that it can ask for passwords,
 * if necessary. Password prompts are disabled automatically when a user cancels
 * the password prompt. This function is to reverse the effect. It does
 * nothing, if the password prompt is not disabled.
 *
 * When the operation is finished, @callback will be called. You can then
 * call e_source_allow_auth_prompt_finish() to get the result of the operation.
 *
 * Since: 3.14
 **/
void
e_source_allow_auth_prompt (ESource *source,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_SOURCE (source));

	task = g_task_new (source, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_source_allow_auth_prompt);

	g_task_run_in_thread (task, source_allow_auth_prompt_thread);

	g_object_unref (task);
}

/**
 * e_source_allow_auth_prompt_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_allow_auth_prompt().
 *
 * If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.14
 **/
gboolean
e_source_allow_auth_prompt_finish (ESource *source,
				   GAsyncResult *result,
				   GError **error)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, source), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, e_source_allow_auth_prompt), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

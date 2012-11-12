/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-source-address-book-config.c - Address Book Configuration.
 *
 * Copyright (C) 2012 Openismus GmbH
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
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

/**
 * SECTION: e-source-address-book-config
 * @include: libebook/libebook.h
 * @short_description: #ESource extension for an address book configuration
 *
 * The #ESourceAddressBookConfig extension adds configuration data to
 * an #ESource which is already defined as an #ESourceAddressBook.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libebook/libebook.h>
 *
 *   ESourceAddressBookConfig *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG);
 * ]|
 **/

#include "e-source-address-book-config.h"
#include "e-book-enumtypes.h"

#define E_SOURCE_ABC_GET_PRIVATE(obj)			\
	(G_TYPE_INSTANCE_GET_PRIVATE			\
	 ((obj), E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG,	\
	  ESourceAddressBookConfigPrivate))

struct _ESourceAddressBookConfigPrivate {
	GMutex *property_lock;
	gchar  *summary_fields;
	gchar  *indexed_fields;
	gboolean revision_guards;
};

enum {
	PROP_0,
	PROP_SUMMARY_FIELDS,
	PROP_INDEXED_FIELDS,
	PROP_REVISION_GUARDS
};

G_DEFINE_TYPE (
	ESourceAddressBookConfig,
	e_source_address_book_config,
	E_TYPE_SOURCE_EXTENSION)


static void
source_address_book_config_finalize (GObject *object)
{
	ESourceAddressBookConfigPrivate *priv;

	priv = E_SOURCE_ABC_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);
	g_free (priv->summary_fields);
	g_free (priv->indexed_fields);

	G_OBJECT_CLASS (e_source_address_book_config_parent_class)->finalize (object);
}

static gchar *
source_address_book_config_dup_litteral_fields (ESourceAddressBookConfig *extension,
						gint                      which)
{
	gchar *duplicate = NULL;

	g_mutex_lock (extension->priv->property_lock);

	switch (which) {
	case PROP_SUMMARY_FIELDS:
		duplicate = g_strdup (extension->priv->summary_fields);
		break;
	case PROP_INDEXED_FIELDS:
		duplicate = g_strdup (extension->priv->indexed_fields);
		break;
	default:
		g_assert_not_reached();
		break;
	}

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

static void
source_address_book_config_set_litteral_fields (ESourceAddressBookConfig *extension,
						const gchar              *litteral_fields,
						gint                      which)
{
	const gchar *property_name;
	gchar **target;

	switch (which) {
	case PROP_SUMMARY_FIELDS:
		target = &(extension->priv->summary_fields);
		property_name = "summary-fields";
		break;
	case PROP_INDEXED_FIELDS:
		target = &(extension->priv->indexed_fields);
		property_name = "indexed-fields";
		break;
	default:
		g_assert_not_reached();
		break;
	}

	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (*target, litteral_fields) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (*target);
	*target = e_util_strdup_strip (litteral_fields);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), property_name);
}

static void
source_address_book_config_set_property (GObject *object,
					 guint property_id,
					 const GValue *value,
					 GParamSpec *pspec)
{
	ESourceAddressBookConfig *extension = E_SOURCE_ADDRESS_BOOK_CONFIG (object);

	switch (property_id) {
	case PROP_SUMMARY_FIELDS:
	case PROP_INDEXED_FIELDS:
		source_address_book_config_set_litteral_fields
			(extension, g_value_get_string (value), property_id);
		return;
	case PROP_REVISION_GUARDS:
		e_source_address_book_config_set_revision_guards_enabled (extension, g_value_get_boolean (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_address_book_config_get_property (GObject *object,
					 guint property_id,
					 GValue *value,
					 GParamSpec *pspec)
{
	ESourceAddressBookConfig *extension = E_SOURCE_ADDRESS_BOOK_CONFIG (object);

	switch (property_id) {
	case PROP_SUMMARY_FIELDS:
	case PROP_INDEXED_FIELDS:
		g_value_take_string (value,
				     source_address_book_config_dup_litteral_fields
				     (extension, property_id));
			return;
	case PROP_REVISION_GUARDS:
		g_value_set_boolean (value, e_source_address_book_config_get_revision_guards_enabled (extension));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_address_book_config_class_init (ESourceAddressBookConfigClass *class)
{
	GObjectClass          *object_class;
	ESourceExtensionClass *extension_class;

	extension_class       = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG;

	object_class               = G_OBJECT_CLASS (class);
	object_class->finalize     = source_address_book_config_finalize;
	object_class->get_property = source_address_book_config_get_property;
	object_class->set_property = source_address_book_config_set_property;

	g_object_class_install_property (
		object_class,
		PROP_SUMMARY_FIELDS,
		g_param_spec_string (
			"summary-fields",
			"Summary Fields",
			"The list of quick reference summary fields for this address book",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_INDEXED_FIELDS,
		g_param_spec_string (
			"indexed-fields",
			"Indexed Fields",
			"The list of summary fields which are to be given indexes in the underlying database",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_REVISION_GUARDS,
		g_param_spec_boolean (
			"revision-guards",
			"Revision Guards",
			"Whether to enable or disable the revision guards",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_type_class_add_private (class, sizeof (ESourceAddressBookConfigPrivate));
}

static void
e_source_address_book_config_init (ESourceAddressBookConfig *extension)
{
	extension->priv = E_SOURCE_ABC_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();
	extension->priv->revision_guards = TRUE;
}

static EContactField *
source_address_book_config_get_fields_array (ESourceAddressBookConfig  *extension,
					     gint                      *n_fields,
					     gint                       which)
{
	EContactField field;
	EContactField *fields = NULL;
	gchar *litteral_fields;
	gchar **split = NULL;
	gint n_ret_fields = 0, i;

	litteral_fields = source_address_book_config_dup_litteral_fields (extension, which);

	if (litteral_fields)
		split = g_strsplit (litteral_fields, ":", 0);

	if (split) {
		n_ret_fields = g_strv_length (split);
		fields       = g_new (EContactField, n_ret_fields);

		for (i = 0; i < n_ret_fields; i++) {
			field = e_contact_field_id (split[i]);

			if (field == 0)
				g_warning ("Unrecognized field '%s' in ESourceAddressBookConfig fields", split[i]);

			fields[i] = field;
		}

		g_strfreev (split);
	}

	g_free (litteral_fields);

	*n_fields = n_ret_fields;

	return fields;
}

static void
e_source_address_book_config_set_fields_array (ESourceAddressBookConfig *extension,
					       EContactField            *fields,
					       gint                      n_fields,
					       gint                      which)
{
	gint i;
	GString *string;
	gboolean malformed = FALSE;

	string = g_string_new ("");

	for (i = 0; i < n_fields; i++) {
		const gchar *field_name = e_contact_field_name (fields[i]);

		if (field_name == NULL) {
			g_warning ("Invalid EContactField given to ESourceAddressBookConfig");
			malformed = TRUE;
			break;
		}

		if (i > 0)
			g_string_append_c (string, ':');

		g_string_append (string, field_name);
	}

	if (malformed == FALSE)
		source_address_book_config_set_litteral_fields (extension, string->str, which);

	g_string_free (string, TRUE);
}

static void
e_source_address_book_config_set_fields_va_list (ESourceAddressBookConfig *extension,
						 va_list                   var_args,
						 gint                      which)
{
	GString *string;
	gboolean malformed = FALSE, first_field = TRUE;
	EContactField field;

	string = g_string_new ("");

	field = va_arg (var_args, EContactField);
	while (field > 0) {
		const gchar *field_name = e_contact_field_name (field);

		if (field_name == NULL) {
			g_warning ("Invalid EContactField given to ESourceAddressBookConfig");
			malformed = TRUE;
			break;
		}

		if (!first_field)
			g_string_append_c (string, ':');
		else
			first_field = FALSE;

		g_string_append (string, field_name);

		field = va_arg (var_args, EContactField);
	}

	if (malformed == FALSE)
		source_address_book_config_set_litteral_fields (extension, string->str, which);

	g_string_free (string, TRUE);
}

/**
 * e_source_address_book_config_get_summary_fields:
 * @extension: An #ESourceAddressBookConfig
 * @n_fields: (out): A return location for the number of #EContactFields in the returned array.
 *
 * Fetches the #EContactFields which are configured to be a part of the summary.
 *
 * <note><para>If there are no configured summary fields, the default configuration is assumed</para></note>
 *
 * Returns: (transfer full): An array of #EContactFields @n_fields long, should be freed with g_free() when done.
 *
 * Since: 3.8
 */
EContactField *
e_source_address_book_config_get_summary_fields (ESourceAddressBookConfig  *extension,
						 gint                      *n_fields)
{
	g_return_val_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension), NULL);
	g_return_val_if_fail (n_fields != NULL, NULL);

	return source_address_book_config_get_fields_array (extension, n_fields, PROP_SUMMARY_FIELDS);
}

/**
 * e_source_address_book_config_set_summary_fieldsv:
 * @extension: An #ESourceAddressBookConfig
 * @fields: The array of #EContactFields to set as summary fields
 * @n_fields: The number of #EContactFields in @fields
 *
 * Sets the summary fields configured for the given addressbook.
 * 
 * The fields %E_CONTACT_UID and %E_CONTACT_REV are not optional,
 * they will be stored in the summary regardless of the configured summary.
 *
 * An empty summary configuration is assumed to be the default summary
 * configuration.
 *
 * <note><para>Only #EContactFields with the type #G_TYPE_STRING or #G_TYPE_BOOLEAN
 * are currently supported as summary fields.</para></note>
 *
 * Since: 3.8
 */
void
e_source_address_book_config_set_summary_fieldsv (ESourceAddressBookConfig *extension,
						  EContactField            *fields,
						  gint                      n_fields)
{
	g_return_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension));
	g_return_if_fail (n_fields >= 0);

	e_source_address_book_config_set_fields_array (extension, fields, n_fields, PROP_SUMMARY_FIELDS);
}

/**
 * e_source_address_book_config_set_summary_fields:
 * @extension: An #ESourceAddressBookConfig
 * @...: A 0 terminated list of #EContactFields to set as summary fields
 *
 * Like e_source_address_book_config_set_summary_fieldsv(), but takes a litteral
 * list of #EContactFields for convenience.
 *
 * To configure the address book summary fields with main phone nubmer fields:
 *
 * |[
 *   #include <libebook/libebook.h>
 *
 *   ESourceAddressBookConfig *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG);
 *
 *   e_source_address_book_config_set_summary_fields (extension, E_CONTACT_PHONE_HOME, E_CONTACT_PHONE_MOBILE, 0);
 * ]|
 *
 * Since: 3.8
 */
void
e_source_address_book_config_set_summary_fields (ESourceAddressBookConfig *extension,
						 ...)
{
	va_list var_args;

	g_return_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension));

	va_start (var_args, extension);
	e_source_address_book_config_set_fields_va_list (extension, var_args, PROP_SUMMARY_FIELDS);
	va_end (var_args);
}

/**
 * e_source_address_book_config_get_indexed_fields:
 * @extension: An #ESourceAddressBookConfig
 * @types: (out) (transfer full): A return location for the set of #EBookIndexTypes corresponding
 *                                to each returned field,  should be freed with g_free() when no longer needed.
 * @n_fields: (out): The number of elements in the returned arrays.
 *
 * Fetches the #EContactFields configured to be indexed, with thier respective #EBookIndexTypes.
 *
 * Returns: (transfer full): The array of indexed #EContactFields.
 */
EContactField  *
e_source_address_book_config_get_indexed_fields (ESourceAddressBookConfig  *extension,
						 EBookIndexType           **types,
						 gint                      *n_fields)
{
	EContactField *ret_fields;
	EBookIndexType *ret_types;
	gboolean malformed = FALSE;
	gchar **split, **index_split;
	gchar *litteral_indexes;
	gint ret_n_fields;
	gint i;

	g_return_val_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension), NULL);
	g_return_val_if_fail (types != NULL, NULL);
	g_return_val_if_fail (n_fields != NULL, NULL);

	litteral_indexes = source_address_book_config_dup_litteral_fields (extension, PROP_INDEXED_FIELDS);
	if (!litteral_indexes) {
		*types = NULL;
		*n_fields = 0;
		return NULL;
	}

	split = g_strsplit (litteral_indexes, ":", 0);
	ret_n_fields = g_strv_length (split);

	ret_fields = g_new0 (EContactField, ret_n_fields);
	ret_types  = g_new0 (EBookIndexType, ret_n_fields);

	for (i = 0; i < ret_n_fields && malformed == FALSE; i++) {

		index_split = g_strsplit (split[i], ",", 2);

		if (index_split[0] && index_split[1]) {
			gint interpreted_enum = 0;

			ret_fields[i] = e_contact_field_id (index_split[0]);

			if (!e_enum_from_string (E_TYPE_BOOK_INDEX_TYPE,
						 index_split[1], &interpreted_enum)) {
				g_warning ("Unknown index type '%s' encountered in indexed fields", index_split[1]);
				malformed = TRUE;
			}

			if (ret_fields[i] <= 0 || ret_fields[i] >= E_CONTACT_FIELD_LAST) {
				g_warning ("Unknown contact field '%s' encountered in indexed fields", index_split[0]);
				malformed = TRUE;
			}

			ret_types[i] = interpreted_enum;
		} else {
			g_warning ("Malformed index definition '%s'", split[i]);
			malformed = TRUE;
		}

		g_strfreev (index_split);
	}

	if (malformed) {
		g_free (ret_fields);
		g_free (ret_types);

		ret_n_fields = 0;
		ret_fields = NULL;
		ret_types = NULL;
	}

	g_strfreev (split);
	g_free (litteral_indexes);

	*n_fields = ret_n_fields;
	*types = ret_types;

	return ret_fields;
}

/**
 * e_source_address_book_config_set_indexed_fieldsv:
 * @extension: An #ESourceAddressBookConfig
 * @fields: The array of #EContactFields to set indexes for
 * @types: The array of #EBookIndexTypes defining what types of indexes to create
 * @n_fields: The number elements in the passed @fields, @rule_types and @rules arrays.
 *
 * Defines indexes for quick reference for the given given #EContactFields in the addressbook.
 *
 * The same #EContactField may be specified multiple times to create multiple indexes
 * with different charachteristics. If an #E_BOOK_INDEX_PREFIX index is created it will
 * be used for #E_BOOK_QUERY_BEGINS_WITH queries; A #E_BOOK_INDEX_SUFFIX index will be
 * constructed efficiently for suffix matching and will be used for #E_BOOK_QUERY_ENDS_WITH queries.
 *
 * Since: 3.8
 */
void
e_source_address_book_config_set_indexed_fieldsv (ESourceAddressBookConfig  *extension,
						  EContactField             *fields,
						  EBookIndexType            *types,
						  gint                       n_fields)
{
	GString *string;
	gboolean malformed = FALSE;
	gint i;

	g_return_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension));
	g_return_if_fail (types != NULL || n_fields <= 0);
	g_return_if_fail (fields != NULL || n_fields <= 0);

	if (n_fields <= 0) {
		source_address_book_config_set_litteral_fields (extension, NULL, PROP_INDEXED_FIELDS);
		return;
	}

	string = g_string_new (NULL);

	for (i = 0; i < n_fields && malformed == FALSE; i++) {
		const gchar *field;
		const gchar *type;
						   
		field = e_contact_field_name (fields[i]);
		type  = e_enum_to_string (E_TYPE_BOOK_INDEX_TYPE, types[i]);

		if (!field) {
			g_warning ("Invalid contact field specified in indexed fields");
			malformed = TRUE;
		} else if (!type) {
			g_warning ("Invalid index type specified in indexed fields");
			malformed = TRUE;
		} else {
			if (i > 0)
				g_string_append_c (string, ':'); 
			g_string_append_printf (string, "%s,%s", field, type);
		}
	}

	if (!malformed)
		source_address_book_config_set_litteral_fields (extension, string->str, PROP_INDEXED_FIELDS);

	g_string_free (string, TRUE);
}

/**
 * e_source_address_book_config_set_indexed_fields:
 * @extension: An #ESourceAddressBookConfig
 * @...: A list of #EContactFields, #EBookIndexType pairs terminated by 0.
 *
 * Like e_source_address_book_config_set_indexed_fieldsv(), but takes a litteral list of
 * of indexes.
 *
 * To give the 'fullname' field an index for prefix and suffix searches:
 *
 * |[
 *   #include <libebook/libebook.h>
 *
 *   ESourceAddressBookConfig *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG);
 *
 *   e_source_address_book_config_set_indexed_fields (extension,
 *                                                    E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
 *                                                    E_CONTACT_FULL_NAME, E_BOOK_INDEX_SUFFIX,
 *                                                    0);
 * ]|
 *
 * Since: 3.8
 */
void
e_source_address_book_config_set_indexed_fields (ESourceAddressBookConfig  *extension,
						  ...)
{
	GString *string;
	gboolean malformed = FALSE, first = TRUE;
	va_list var_args;
	EContactField field_in;
	EBookIndexType type_in;

	g_return_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension));

	string = g_string_new (NULL);

	va_start (var_args, extension);

	field_in = va_arg (var_args, EContactField);
	while (field_in > 0 && malformed == FALSE) {
		const gchar *field;
		const gchar *type;

		field = e_contact_field_name (field_in);
		if (field == NULL) {
			g_warning ("Invalid contact field specified in "
				   "e_source_address_book_config_set_indexed_fields()");
			malformed = TRUE;
			break;
		}

		type_in = va_arg (var_args, EBookIndexType);
		type = e_enum_to_string (E_TYPE_BOOK_INDEX_TYPE, type_in);
		if (type == NULL) {
			g_warning ("Invalid index type "
				   "e_source_address_book_config_set_indexed_fields()");
			malformed = TRUE;
			break;
		}

		if (!first)
			g_string_append_c (string, ':'); 
		else
			first = FALSE;

		g_string_append_printf (string, "%s,%s", field, type);

		/* Continue loop until first 0 found... */
		field_in = va_arg (var_args, EContactField);
	}
	va_end (var_args);

	if (!malformed)
		source_address_book_config_set_litteral_fields (extension, string->str, PROP_INDEXED_FIELDS);

	g_string_free (string, TRUE);
}

/**
 * e_source_address_book_config_set_revision_guards_enabled:
 * @extension: An #ESourceAddressBookConfig
 * @enabled: Whether to enable or disable the revision guards.
 *
 * Enables or disables the revision guards in the address book backend. If revision
 * guards are enabled, then contact modifications will be refused with the
 * error %E_DATA_BOOK_STATUS_BAD_REVISION if the modified contact revision is out of date.
 *
 * This avoids data loss when multiple processes write to the addressbook by forcing
 * the calling process to get an updated contact before committing it to the addressbook.
 *
 * Revision guards are enabled by default.
 *
 * Since: 3.8
 */
void
e_source_address_book_config_set_revision_guards_enabled (ESourceAddressBookConfig  *extension,
							  gboolean                   enabled)
{
	g_return_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension));

	g_mutex_lock (extension->priv->property_lock);

	if (extension->priv->revision_guards == enabled) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	extension->priv->revision_guards = enabled;

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "revision-guards");
}


/**
 * e_source_address_book_config_get_revision_guards_enabled:
 * @extension: An #ESourceAddressBookConfig
 *
 * Checks whether revision guards in the address book backend are enabled.
 *
 * Returns: %TRUE if the revision guards are enabled.
 *
 * Since: 3.8
 */
gboolean
e_source_address_book_config_get_revision_guards_enabled (ESourceAddressBookConfig *extension)
{
	gboolean enabled;

	g_return_val_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension), FALSE);

	g_mutex_lock (extension->priv->property_lock);
	enabled = extension->priv->revision_guards;
	g_mutex_unlock (extension->priv->property_lock);

	return enabled;
}

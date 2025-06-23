/*
 * Copyright 2025 Collabora, Ltd. (https://www.collabora.com)
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
 */

#include "evolution-data-server-config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-enums.h"
#include "camel-enumtypes.h"
#include "camel-file-utils.h"
#include "camel-stateful-object.h"

G_DEFINE_INTERFACE (CamelStatefulObject, camel_stateful_object, G_TYPE_OBJECT)

/* State file for CamelStatefulObject data.
 * Any later versions should only append data.
 *
 * version:uint32
 *
 * Version 0 of the file:
 *
 * version:uint32 = 0
 * count:uint32					-- count of meta-data items
 * ( name:string value:string ) *count		-- meta-data items
 *
 * Version 1 of the file adds:
 * count:uint32					-- count of persistent properties
 * ( tag:uing32 value:tagtype ) *count		-- persistent properties
 */

#define CAMEL_STATEFUL_OBJECT_STATE_FILE_MAGIC "CLMD"

/* XXX This is a holdover from Camel's old homegrown type system.
 *     CamelArg was a kind of primitive version of GObject properties.
 *     The argument ID and data type were encoded into a 32-bit integer.
 *     Unfortunately the encoding was also used in the binary state file
 *     format, so we still need the secret decoder ring. */
enum camel_arg_t {
	CAMEL_ARG_END = 0,
	CAMEL_ARG_IGNORE = 1,	/* override/ignore an arg in-place */

	CAMEL_ARG_FIRST = 1024,	/* 1024 args reserved for arg system */

	CAMEL_ARG_TYPE = 0xf0000000, /* type field for tags */
	CAMEL_ARG_TAG = 0x0fffffff, /* tag field for args */

	CAMEL_ARG_OBJ = 0x00000000, /* object */
	CAMEL_ARG_INT = 0x10000000, /* gint */
	CAMEL_ARG_DBL = 0x20000000, /* gdouble */
	CAMEL_ARG_STR = 0x30000000, /* c string */
	CAMEL_ARG_PTR = 0x40000000, /* ptr */
	CAMEL_ARG_BOO = 0x50000000, /* bool */
	CAMEL_ARG_3ST = 0x60000000, /* three-state */
	CAMEL_ARG_I64 = 0x70000000  /* gint64 */
};

#define CAMEL_ARGV_MAX (20)

static guint
hash_table_reverse_find (GHashTable *table, guint32 tag)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, table);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint32 val = GPOINTER_TO_UINT (value);
		if (val == tag)
			return GPOINTER_TO_UINT (key);
	}

	return 0;
}

static guint32
get_property_tag (CamelStatefulObject *self, GParamSpec *pspec)
{
	CamelStatefulObjectInterface *iface;

	if (G_LIKELY (G_TYPE_FROM_INSTANCE(self) == pspec->owner_type))
		iface = CAMEL_STATEFUL_OBJECT_GET_IFACE (self);
	else
		iface = g_type_interface_peek (g_type_class_peek (pspec->owner_type), CAMEL_TYPE_STATEFUL_OBJECT);

	if (!iface || !iface->get_property_tag)
		return 0;

	return iface->get_property_tag (self, pspec->param_id);
}

static GParamSpec *
get_property_from_tag (CamelStatefulObject *self,
		       guint32 tag)
{
	CamelStatefulObjectInterface *iface;
	GObjectClass *class;
	GParamSpec **properties;
	guint ii, n_properties;

	class = G_OBJECT_GET_CLASS (self);
	properties = g_object_class_list_properties (class, &n_properties);

	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = properties[ii];

		if (G_LIKELY (G_TYPE_FROM_INSTANCE(self) == pspec->owner_type))
			iface = CAMEL_STATEFUL_OBJECT_GET_IFACE (self);
		else
			iface = g_type_interface_peek (g_type_class_peek (pspec->owner_type), CAMEL_TYPE_STATEFUL_OBJECT);

		if (!iface || !iface->get_property_tag)
			continue;

		if (iface->get_property_tag (self, pspec->param_id) == tag) {
			g_free (properties);
			return pspec;
		}
	}

	g_free (properties);
	return NULL;
}


static gboolean
object_state_read (CamelStatefulObject *self,
                   FILE *fp)
{
	GValue gvalue = G_VALUE_INIT;
	guint32 count, version;
	guint ii;

	if (camel_file_util_decode_uint32 (fp, &version) == -1)
		return FALSE;

	if (version > 2)
		return FALSE;

	if (camel_file_util_decode_uint32 (fp, &count) == -1)
		return FALSE;

	/* XXX Camel no longer supports meta-data in state
	 *     files, so we're just eating dead data here. */
	for (ii = 0; ii < count; ii++) {
		gchar *name = NULL;
		gchar *value = NULL;
		gboolean success;

		success =
			camel_file_util_decode_string (fp, &name) == 0 &&
			camel_file_util_decode_string (fp, &value) == 0;

		g_free (name);
		g_free (value);

		if (!success)
			return FALSE;
	}

	if (version == 0)
		return TRUE;

	if (camel_file_util_decode_uint32 (fp, &count) == -1)
		return TRUE;

	if (count == 0 || count > 1024)
		/* Maybe it was just version 0 after all. */
		return TRUE;

	count = MIN (count, CAMEL_ARGV_MAX);

	for (ii = 0; ii < count; ii++) {
		gboolean property_set = FALSE;
		GParamSpec *pspec;
		guint32 tag, v_uint32;
		gint32 v_int32;
		gint64 v_int64;

		if (camel_file_util_decode_uint32 (fp, &tag) == -1)
			goto failure;

		/* Record state file values into GValues.
		 * XXX We currently only support booleans and three-state. */
		switch (tag & CAMEL_ARG_TYPE) {
			case CAMEL_ARG_BOO:
				if (camel_file_util_decode_uint32 (fp, &v_uint32) == -1)
					goto failure;
				g_value_init (&gvalue, G_TYPE_BOOLEAN);
				g_value_set_boolean (&gvalue, (gboolean) v_uint32);
				break;
			case CAMEL_ARG_INT:
				if (camel_file_util_decode_fixed_int32 (fp, &v_int32) == -1)
					goto failure;
				g_value_init (&gvalue, G_TYPE_INT);
				g_value_set_int (&gvalue, v_int32);
				break;
			case CAMEL_ARG_3ST:
				if (camel_file_util_decode_uint32 (fp, &v_uint32) == -1)
					goto failure;
				g_value_init (&gvalue, CAMEL_TYPE_THREE_STATE);
				g_value_set_enum (&gvalue, (CamelThreeState) v_uint32);
				break;
			case CAMEL_ARG_I64:
				if (camel_file_util_decode_gint64 (fp, &v_int64) == -1)
					goto failure;
				g_value_init (&gvalue, G_TYPE_INT64);
				g_value_set_int64 (&gvalue, v_int64);
				break;
			default:
				g_warn_if_reached ();
				goto failure;
		}

		/* Now we have to match the legacy numeric CamelArg tag
		 * value with a GObject property.  The GObject property
		 * IDs have been set to the same legacy tag values, but
		 * we have to access a private GParamSpec field to get
		 * to them (pspec->param_id). */

		tag &= CAMEL_ARG_TAG;  /* filter out the type code */
		pspec = get_property_from_tag (self, tag);
		if (!pspec) {
			/* XXX This tag was used by the old IMAP backend.
			 *     It may still show up in accounts that were
			 *     migrated from IMAP to IMAPX.  Silence the
			 *     warning. */
			if (tag != 0x2500)
				g_warning (
					"Could not find a corresponding %s "
					"property for state file tag 0x%x",
					G_OBJECT_TYPE_NAME (self), tag);
			g_value_unset (&gvalue);
			continue;
		}

		if (version == 1 && pspec->value_type == CAMEL_TYPE_THREE_STATE &&
		    G_VALUE_HOLDS_BOOLEAN (&gvalue)) {
			/* Convert from boolean to three-state value. Assign the 'TRUE' to 'On'
			   and the rest keep as 'Inconsistent'. */
			gboolean stored = g_value_get_boolean (&gvalue);

			g_value_unset (&gvalue);
			g_value_init (&gvalue, CAMEL_TYPE_THREE_STATE);
			g_value_set_enum (&gvalue, stored ? CAMEL_THREE_STATE_ON : CAMEL_THREE_STATE_INCONSISTENT);
		}

		g_object_set_property (G_OBJECT (self), pspec->name, &gvalue);

		g_value_unset (&gvalue);
	}

	return TRUE;

failure:
	return FALSE;
}

static gboolean
object_state_write (CamelStatefulObject *self,
                    FILE *fp)
{
	GValue value = G_VALUE_INIT;
	GObjectClass *class;
	GParamSpec **properties;
	guint ii, n_properties;
	guint32 n_persistent = 0;

	/* Version = 2 */
	if (camel_file_util_encode_uint32 (fp, 2) == -1)
		return TRUE;

	/* No meta-data items. */
	if (camel_file_util_encode_uint32 (fp, 0) == -1)
		return TRUE;

	class = G_OBJECT_GET_CLASS (self);
	properties = g_object_class_list_properties (class, &n_properties);

	/* Count persistent properties. */
	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = properties[ii];
		if (get_property_tag (self, pspec))
			n_persistent++;
	}

	if (camel_file_util_encode_uint32 (fp, n_persistent) == -1)
		goto exit;

	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = properties[ii];
		guint32 tag, v_uint32;
		gint32 v_int32;
		gint64 v_int64;

		tag = get_property_tag (self, pspec);
		if (tag == 0)
			continue;

		g_value_init (&value, pspec->value_type);

		g_object_get_property (G_OBJECT (self), pspec->name, &value);

		/* Record the GValue to the state file.
		 * XXX We currently only support booleans. */
		switch (pspec->value_type) {
			case G_TYPE_BOOLEAN:
				tag |= CAMEL_ARG_BOO;
				v_uint32 = g_value_get_boolean (&value);
				if (camel_file_util_encode_uint32 (fp, tag) == -1)
					goto exit;
				if (camel_file_util_encode_uint32 (fp, v_uint32) == -1)
					goto exit;
				break;
			case G_TYPE_INT:
				tag |= CAMEL_ARG_INT;
				v_int32 = g_value_get_int (&value);
				if (camel_file_util_encode_uint32 (fp, tag) == -1)
					goto exit;
				if (camel_file_util_encode_fixed_int32 (fp, v_int32) == -1)
					goto exit;
				break;
			case G_TYPE_INT64:
				tag |= CAMEL_ARG_I64;
				v_int64 = g_value_get_int64 (&value);
				if (camel_file_util_encode_uint32 (fp, tag) == -1)
					goto exit;
				if (camel_file_util_encode_gint64 (fp, v_int64) == -1)
					goto exit;
				break;
			default:
				if (pspec->value_type == CAMEL_TYPE_THREE_STATE) {
					tag |= CAMEL_ARG_3ST;
					v_uint32 = g_value_get_enum (&value);
					if (camel_file_util_encode_uint32 (fp, tag) == -1)
						goto exit;
					if (camel_file_util_encode_uint32 (fp, v_uint32) == -1)
						goto exit;
				} else {
					g_warn_if_reached ();
					goto exit;
				}
				break;
		}

		g_value_unset (&value);
	}

exit:
	g_free (properties);

	return TRUE;
}

static void
camel_stateful_object_default_init (CamelStatefulObjectInterface *iface)
{
}

/**
 * camel_stateful_object_read_state:
 * @self: a #CamelStatefulObject
 *
 * Read persistent object state from file.
 *
 * Returns: %TRUE on success.
 *
 * Since: 3.58
 **/
gint
camel_stateful_object_read_state (CamelStatefulObject *self)
{
	const gchar *state_filename;
	gboolean res = FALSE;
	FILE *fp;
	gchar magic[4];

	g_return_val_if_fail (CAMEL_IS_STATEFUL_OBJECT (self), -1);

	state_filename = camel_stateful_object_get_state_file (self);
	if (state_filename == NULL)
		return TRUE;

	fp = g_fopen (state_filename, "rb");
	if (fp != NULL) {
		if (fread (magic, 4, 1, fp) == 1
		    && memcmp (magic, CAMEL_STATEFUL_OBJECT_STATE_FILE_MAGIC, 4) == 0)
			res = object_state_read (self, fp);
		fclose (fp);
	}

	return res;
}

/**
 * camel_stateful_object_write_state:
 * @self: a #CamelStatefulObject
 *
 * Write persistent object state file.
 *
 * Returns: %TRUE on success.
 *
 * Since: 3.58
 **/
gboolean
camel_stateful_object_write_state (CamelStatefulObject *self)
{
	const gchar *state_filename;
	gchar *savename, *dirname;
	gboolean res = FALSE;
	FILE *fp;

	g_return_val_if_fail (CAMEL_IS_STATEFUL_OBJECT (self), -1);

	state_filename = camel_stateful_object_get_state_file (self);
	if (state_filename == NULL)
		return TRUE;

	savename = camel_file_util_savename (state_filename);

	dirname = g_path_get_dirname (savename);
	g_mkdir_with_parents (dirname, 0700);
	g_free (dirname);

	fp = g_fopen (savename, "wb");
	if (fp != NULL) {
		if (fwrite (CAMEL_STATEFUL_OBJECT_STATE_FILE_MAGIC, 4, 1, fp) == 1
		    && object_state_write (self, fp)) {
			if (fclose (fp) == 0) {
				res = TRUE;
				if (g_rename (savename, state_filename) == -1)
					res = FALSE;
			}
		} else {
			fclose (fp);
		}
	} else {
		g_warning ("Could not save object state file to '%s': %s", savename, g_strerror (errno));
	}

	g_free (savename);

	return res;
}

/**
 * camel_stateful_object_get_state_file:
 * @self: a #CamelStatefulObject
 *
 * Returns the name of the file in which persistent property values for
 * @self are stored.  The file is used by camel_stateful_object_write_state()
 * and camel_stateful_object_read_state() to save and restore object state.
 *
 * Returns: the name of the persistent property file
 *
 * Since: 3.58
 **/
const gchar *
camel_stateful_object_get_state_file (CamelStatefulObject *self)
{
	CamelStatefulObjectInterface *iface;

	g_return_val_if_fail (CAMEL_IS_STATEFUL_OBJECT (self), NULL);

	iface = CAMEL_STATEFUL_OBJECT_GET_IFACE (self);
	g_return_val_if_fail (iface->get_state_file != NULL, NULL);
	return iface->get_state_file (self);
}

/**
 * camel_stateful_object_delete_state_file:
 * @self: a #CamelStatefulObject
 * @error: a #GError
 *
 * Convenient function to delete the state file.
 *
 * Returns: %TRUE on success.
 *
 * Since: 3.58
 **/
gboolean
camel_stateful_object_delete_state_file (CamelStatefulObject *self,
					 GError **error)
{
	const gchar *state_file;

	g_return_val_if_fail (CAMEL_IS_STATEFUL_OBJECT (self), FALSE);

	state_file = camel_stateful_object_get_state_file (self);
	if (g_unlink (state_file) == -1 && errno != ENOENT && errno != ENOTDIR) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not delete folder meta file “%s”: %s"),
			state_file,
			g_strerror (errno));
	}

	return TRUE;
}

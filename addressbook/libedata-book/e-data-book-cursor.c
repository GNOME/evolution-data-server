/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
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
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

/**
 * SECTION: e-data-book-cursor
 * @include: libedata-book/libedata-book.h
 * @short_description: The abstract cursor API
 *
 * The #EDataBookCursor API is the high level cursor API on the
 * addressbook server, it can respond to client requests directly
 * when opened in direct read access mode, otherwise it will implement
 * the org.gnome.evolution.dataserver.AddressBookCursor D-Bus interface
 * when instantiated by the addressbook server.
 *
 * <note><para>EDataBookCursor is an implementation detail for backends who wish
 * to implement cursors. If you need to use the client API to iterate over contacts
 * stored in Evolution Data Server; you should be using #EBookClientCursor instead.
 * </para></note>
 *
 * <refsect2 id="cursor-implementing">
 * <title>Implementing Cursors</title>
 * <para>
 * In order for an addressbook backend to implement cursors, it must
 * first be locale aware, persist a current locale setting and implement
 * the #EBookBackendClass.set_locale() and #EBookBackendClass.get_locale()
 * methods.
 * </para>
 * <para>
 * The backend indicates that it supports cursors by implementing the
 * #EBookBackendClass.create_cursor() and returning an #EDataBookCursor,
 * any backend implementing #EBookBackendClass.create_cursor() should also
 * implement #EBookBackendClass.delete_cursor().
 * </para>
 * <para>
 * For backends which use #EBookBackendSqliteDB to store contacts,
 * an #EDataBookCursorSqlite can be used as a cursor implementation.
 * </para>
 * <para>
 * Implementing a concrete cursor class for your own addressbook
 * backend is a matter of implementing all of the virtual methods
 * on the #EDataBookCursorClass vtable, each virtual method has
 * documentation describing how each of the methods should be implemented.
 * </para>
 * </refsect2>
 *
 * <refsect2 id="cursor-track-state">
 * <title>Tracking Cursor State</title>
 * <para>
 * The cursor state itself is defined as an array of sort keys
 * and an %E_CONTACT_UID value. There should be one sort key
 * stored for each contact field which was passed to
 * #EBookBackendClass.create_cursor().
 * </para>
 * <para>
 * Cursor position is always set to last result which
 * was traversed in the most recent query. After moving the
 * cursor through the next batch of results, the last result
 * should always be saved as the new current cursor state, unless
 * the end of the contact list is reached and the requested amount
 * of contacts could not be returned, in which case the cursor
 * should reset itself to a null state (the cursor walks off the
 * end of the results into a reset state).
 * </para>
 * <para>
 * The cursor must track two states at all times, one
 * state which is the current cursor position, and one
 * state which was the previous cursor position in order
 * to satisfy the %E_BOOK_CURSOR_ORIGIN_PREVIOUS origin
 * in a call to #EDataBookCursorClass.move_by().
 * </para>
 * <para>
 * Calls to #EDataBookCursorClass.move_by() with the
 * %E_BOOK_CURSOR_ORIGIN_RESET origin resets both current
 * and previous cursor states before proceeding to move.
 * Calls to #EDataBookCursorClass.set_alphabetic_index() also
 * effect both cursor states.
 * </para>
 * </refsect2>
 *
 * <refsect2 id="cursor-localized-sorting">
 * <title>Implementing Localized Sorting</title>
 * <para>
 * To implement localized sorting in an addressbook backend, an #ECollator
 * can be used. The #ECollator provides all the functionality needed
 * to respond to the cursor methods.
 * </para>
 * <para>
 * When storing contacts in your backend, sort keys should be generated
 * for any fields which might be used as sort key parameters for a cursor,
 * keys for these fields should be generated with e_collator_generate_key()
 * using an #ECollator created for the locale in which your addressbook is
 * currently configured (undesired fields may be rejected at cursor creation
 * time with an %E_CLIENT_ERROR_INVALID_QUERY error).
 * </para>
 * <para>
 * The generated sort keys can then be used with strcmp() in order to
 * compare results with the currently stored cursor state. These comparisons
 * should compare contact fields in order of precedence in the array of
 * sort fields which the cursor was configured with. If two contacts match
 * exactly, then the %E_CONTACT_UID value is used to determine which
 * contact sorts above or below the other.
 * </para>
 * <para>
 * When your sort keys are generated using #ECollator, you can easily
 * use e_collator_generate_key_for_index() to implement
 * #EDataBookCursorClass.set_alphabetic_index() and set the cursor
 * position before (below) a given letter in the active alphabet. The key
 * generated for an alphabetic index is guaranteed to sort below any word
 * starting with the given letter, and above any word starting with the
 * preceeding letter.
 * </para>
 * </refsect2>
 *
 * <refsect2 id="cursor-dra">
 * <title>Direct Read Access</title>
 * <para>
 * In order to support cursors in backends which support Direct Read Access
 * mode, the underlying addressbook data must be written atomically along with each
 * new revision attribute. The cursor mechanics rely on this atomicity in order
 * to avoid any race conditions and ensure that data read back from the addressbook
 * is current and up to date.
 * </para>
 * </refsect2>
 *
 * <refsect2 id="cursor-backends">
 * <title>Backend Tasks</title>
 * <para>
 * Backends have ownership of the cursors which they create
 * and have some responsibility when supporting cursors.
 * </para>
 * <para>
 * As mentioned above, in Direct Read Access mode (if supported), all
 * revision writes and addressbook modifications must be committed
 * atomically.
 * </para>
 * <para>
 * Beyond that, it is the responsibility of the backend to call
 * e_data_book_cursor_contact_added() and e_data_book_cursor_contact_removed()
 * whenever the addressbook is modified. When a contact is modified
 * but not added or removed, then e_data_book_cursor_contact_removed()
 * should be called with the old existing contact and then
 * e_data_book_cursor_contact_added() should be called with
 * the new contact. This will automatically update the cursor
 * total and position status.
 * </para>
 * <para>
 * Note that if it's too much trouble to load the existing
 * contact data when a contact is modified, then
 * e_data_book_cursor_recalculate() can be called instead. This
 * will use the #EDataBookCursorClass.get_position() method
 * recalculate current cursor position from scratch.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "e-data-book-cursor.h"
#include "e-book-backend.h"

/* Private D-Bus class. */
#include <e-dbus-address-book-cursor.h>

/* GObjectClass */
static void e_data_book_cursor_constructed (GObject *object);
static void e_data_book_cursor_dispose (GObject *object);
static void e_data_book_cursor_finalize (GObject *object);
static void e_data_book_cursor_get_property (GObject *object,
					     guint property_id,
					     GValue *value,
					     GParamSpec *pspec);
static void e_data_book_cursor_set_property (GObject *object,
					     guint property_id,
					     const GValue *value,
					     GParamSpec *pspec);

/* Private Functions */
static void     data_book_cursor_set_values      (EDataBookCursor  *cursor,
						  gint              total,
						  gint              position);
static gboolean data_book_cursor_compare_contact (EDataBookCursor  *cursor,
						  EContact         *contact,
						  gint             *result,
						  gboolean         *matches_sexp);
static void     calculate_move_by_position       (EDataBookCursor  *cursor,
						  EBookCursorOrigin origin,
						  gint              count,
						  gint              results);

/* D-Bus callbacks */
static gint     data_book_cursor_handle_move_by              (EDBusAddressBookCursor *dbus_object,
							      GDBusMethodInvocation  *invocation,
							      const gchar            *revision,
							      EBookCursorOrigin       origin,
							      gint                    count,
							      gboolean                fetch_results,
							      EDataBookCursor        *cursor);
static gboolean data_book_cursor_handle_set_alphabetic_index (EDBusAddressBookCursor *dbus_object,
							      GDBusMethodInvocation  *invocation,
							      gint                    index,
							      const gchar            *locale,
							      EDataBookCursor        *cursor);
static gboolean data_book_cursor_handle_set_query            (EDBusAddressBookCursor *dbus_object,
							      GDBusMethodInvocation  *invocation,
							      const gchar            *query,
							      EDataBookCursor        *cursor);
static gboolean data_book_cursor_handle_dispose              (EDBusAddressBookCursor *dbus_object,
							      GDBusMethodInvocation  *invocation,
							      EDataBookCursor        *cursor);

struct _EDataBookCursorPrivate {
	EDBusAddressBookCursor *dbus_object;
	EBookBackend *backend;

	gchar *locale;
	gint total;
	gint position;
};

enum {
	PROP_0,
	PROP_BACKEND,
	PROP_TOTAL,
	PROP_POSITION,
};

G_DEFINE_ABSTRACT_TYPE (
	EDataBookCursor,
	e_data_book_cursor,
	G_TYPE_OBJECT);

/************************************************
 *                  GObjectClass                *
 ************************************************/
static void
e_data_book_cursor_class_init (EDataBookCursorClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->constructed = e_data_book_cursor_constructed;
	object_class->finalize = e_data_book_cursor_finalize;
	object_class->dispose = e_data_book_cursor_dispose;
	object_class->get_property = e_data_book_cursor_get_property;
	object_class->set_property = e_data_book_cursor_set_property;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND,
		g_param_spec_object (
			"backend",
			"Backend",
			"The backend which created this cursor",
			E_TYPE_BOOK_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_TOTAL,
		g_param_spec_int (
			"total", "Total",
			"The total results for this cursor",
			0, G_MAXINT, 0,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_POSITION,
		g_param_spec_int (
			"position", "Position",
			"The current position of this cursor",
			0, G_MAXINT, 0,
			G_PARAM_READABLE));

	g_type_class_add_private (class, sizeof (EDataBookCursorPrivate));
}

static void
e_data_book_cursor_init (EDataBookCursor *cursor)
{
	cursor->priv = 
	  G_TYPE_INSTANCE_GET_PRIVATE (cursor,
				       E_TYPE_DATA_BOOK_CURSOR,
				       EDataBookCursorPrivate);
}

static void
e_data_book_cursor_constructed (GObject *object)
{
  EDataBookCursor *cursor = E_DATA_BOOK_CURSOR (object);
  GError *error = NULL;

  /* Get the initial cursor values */
  if (!e_data_book_cursor_recalculate (cursor, &error)) {
	  g_warning ("Failed to calculate initial cursor position: %s", error->message);
	  g_clear_error (&error);
  }

  G_OBJECT_CLASS (e_data_book_cursor_parent_class)->constructed (object);
}

static void
e_data_book_cursor_finalize (GObject *object)
{
  EDataBookCursor        *cursor = E_DATA_BOOK_CURSOR (object);
  EDataBookCursorPrivate *priv = cursor->priv;

  g_free (priv->locale);

  G_OBJECT_CLASS (e_data_book_cursor_parent_class)->finalize (object);
}

static void
e_data_book_cursor_dispose (GObject *object)
{
  EDataBookCursor        *cursor = E_DATA_BOOK_CURSOR (object);
  EDataBookCursorPrivate *priv = cursor->priv;

  g_clear_object (&(priv->dbus_object));
  g_clear_object (&(priv->backend));

  G_OBJECT_CLASS (e_data_book_cursor_parent_class)->dispose (object);
}

static void
e_data_book_cursor_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	EDataBookCursor        *cursor = E_DATA_BOOK_CURSOR (object);
	EDataBookCursorPrivate *priv = cursor->priv;

	switch (property_id) {
	case PROP_BACKEND:
		g_value_set_object (value, priv->backend);
		break;
	case PROP_TOTAL:
		g_value_set_int (value, priv->total);
		break;
	case PROP_POSITION:
		g_value_set_int (value, priv->position);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
e_data_book_cursor_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	EDataBookCursor        *cursor = E_DATA_BOOK_CURSOR (object);
	EDataBookCursorPrivate *priv = cursor->priv;

	switch (property_id) {
	case PROP_BACKEND:
		/* Construct-only, can only be set once */
		priv->backend = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/************************************************
 *                Private Functions             *
 ************************************************/
static void
data_book_cursor_set_values (EDataBookCursor *cursor,
			     gint             total,
			     gint             position)
{
	EDataBookCursorPrivate *priv;
	gboolean changed = FALSE;

	g_return_if_fail (E_IS_DATA_BOOK_CURSOR (cursor));

	priv = cursor->priv;

	g_object_freeze_notify (G_OBJECT (cursor));

	if (priv->total != total) {
		priv->total = total;
		g_object_notify (G_OBJECT (cursor), "total");
		changed = TRUE;
	}

	if (priv->position != position) {
		priv->position = position;
		g_object_notify (G_OBJECT (cursor), "position");
		changed = TRUE;
	}

	g_object_thaw_notify (G_OBJECT (cursor));

	if (changed && priv->dbus_object) {
		e_dbus_address_book_cursor_set_total (priv->dbus_object, priv->total);
		e_dbus_address_book_cursor_set_position (priv->dbus_object, priv->position);
	}
}

static gboolean
data_book_cursor_compare_contact (EDataBookCursor     *cursor,
				  EContact            *contact,
				  gint                *result,
				  gboolean            *matches_sexp)
{
	if (!E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->compare_contact)
		return FALSE;

	g_object_ref (cursor);
	*result = (* E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->compare_contact) (cursor,
									      contact,
									      matches_sexp);
	g_object_unref (cursor);

	return TRUE;
}

static void
calculate_move_by_position (EDataBookCursor     *cursor,
			    EBookCursorOrigin    origin,
			    gint                 count,
			    gint                 results)
{
	EDataBookCursorPrivate *priv = cursor->priv;
	GError *error = NULL;
	gint new_position;

	switch (origin) {
	case E_BOOK_CURSOR_ORIGIN_CURRENT:

		if (count < 0 && priv->position == 0)
			new_position = (priv->total + 1) + count;
		else
			new_position = priv->position + count;

		/* If we ran off the end of the total query, reset to 0 */
		if (new_position < 0 || new_position > priv->total)
			new_position = 0;

		data_book_cursor_set_values (cursor, priv->total, new_position);
		break;

	case E_BOOK_CURSOR_ORIGIN_PREVIOUS:
		/* We don't manually track the previous position, so for now
		 * we need to recalculate the position entirely
		 */
		if (!e_data_book_cursor_recalculate (cursor, &error)) {
			g_warning ("Failed to recalculate the cursor value "
				   "after moving the cursor: %s",
				   error->message);
			g_clear_error (&error);
		}
		break;
	case E_BOOK_CURSOR_ORIGIN_RESET:

		if (count < 0)
			new_position = (priv->total + 1) + count;
		else
			new_position = count;

		/* If we ran off the end of the total query, reset to 0 */
		if (new_position < 0 || new_position > priv->total)
			new_position = 0;

		data_book_cursor_set_values (cursor, priv->total, new_position);
		break;

	}
}

/************************************************
 *                D-Bus Callbacks               *
 ************************************************/
static gboolean
data_book_cursor_handle_move_by (EDBusAddressBookCursor *dbus_object,
				 GDBusMethodInvocation  *invocation,
				 const gchar            *revision,
				 EBookCursorOrigin       origin,
				 gint                    count,
				 gboolean                fetch_results,
				 EDataBookCursor        *cursor)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint n_results;

	n_results = e_data_book_cursor_move_by (cursor, revision, origin, count,
						fetch_results ? &results : NULL,
						&error);

	if (n_results < 0) {
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_clear_error (&error);
	} else {
		gchar **strv = NULL;
		const gchar * const empty_str[] = { NULL };

		if (results) {
			GSList *l;
			gint i = 0;

			strv = g_new0 (gchar *, g_slist_length (results) + 1);

			for (l = results; l; l = l->next) {
				gchar *vcard = l->data;

				strv[i++] = e_util_utf8_make_valid (vcard);
			}

		}

		e_dbus_address_book_cursor_complete_move_by (dbus_object,
							     invocation,
							     n_results,
							     strv ? 
							     (const gchar *const *)strv :
							     empty_str,
							     cursor->priv->total,
							     cursor->priv->position);


		g_strfreev (strv);
	}

	return TRUE;
}

static gboolean
data_book_cursor_handle_set_alphabetic_index (EDBusAddressBookCursor *dbus_object,
					      GDBusMethodInvocation  *invocation,
					      gint                    index,
					      const gchar            *locale,
					      EDataBookCursor        *cursor)
{
	GError *error = NULL;

	if (!e_data_book_cursor_set_alphabetic_index (cursor,
						      index,
						      locale,
						      &error)) {
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_clear_error (&error);
	} else {
		e_dbus_address_book_cursor_complete_set_alphabetic_index (dbus_object,
									  invocation,
									  cursor->priv->total,
									  cursor->priv->position);
	}

	return TRUE;
}

static gboolean
data_book_cursor_handle_set_query (EDBusAddressBookCursor *dbus_object,
				   GDBusMethodInvocation  *invocation,
				   const gchar            *query,
				   EDataBookCursor        *cursor)
{
	GError *error = NULL;

	if (!e_data_book_cursor_set_sexp (cursor, query, &error)) {
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_clear_error (&error);
	} else {
		e_dbus_address_book_cursor_complete_set_query (dbus_object,
							       invocation,
							       cursor->priv->total,
							       cursor->priv->position);
	}

	return TRUE;
}

static gboolean
data_book_cursor_handle_dispose (EDBusAddressBookCursor *dbus_object,
				 GDBusMethodInvocation  *invocation,
				 EDataBookCursor        *cursor)
{
	EDataBookCursorPrivate *priv = cursor->priv;
	GError *error = NULL;

	/* The backend will release the cursor, just make sure that
	 * we survive long enough to complete this method call
	 */
	g_object_ref (cursor);

	/* This should never really happen, but if it does, there is no
	 * we cannot expect the client to recover well from an error at
	 * dispose time, so let's just log the warning.
	 */
	if (!e_book_backend_delete_cursor (priv->backend, cursor, &error)) {
		g_warning ("Error trying to delete cursor: %s", error->message);
		g_clear_error (&error);
	}
	e_dbus_address_book_cursor_complete_dispose (dbus_object, invocation);

	g_object_unref (cursor);

	return TRUE;
}

/************************************************
 *                       API                    *
 ************************************************/

/**
 * e_data_book_cursor_get_backend:
 * @cursor: an #EDataBookCursor
 *
 * Gets the backend which created and owns @cursor.
 *
 * Returns: (transfer none): The #EBookBackend owning @cursor.
 *
 * Since: 3.12
 */
struct _EBookBackend *
e_data_book_cursor_get_backend (EDataBookCursor *cursor)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), NULL);

	return cursor->priv->backend;
}


/**
 * e_data_book_cursor_get_total:
 * @cursor: an #EDataBookCursor
 *
 * Fetch the total number of contacts which match @cursor's query expression.
 *
 * Returns: the total contacts for @cursor
 *
 * Since: 3.12
 */
gint
e_data_book_cursor_get_total (EDataBookCursor *cursor)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), -1);

	return cursor->priv->total;
}

/**
 * e_data_book_cursor_get_position:
 * @cursor: an #EDataBookCursor
 *
 * Fetch the current position of @cursor in its result list.
 *
 * Returns: the current position of @cursor
 *
 * Since: 3.12
 */
gint
e_data_book_cursor_get_position (EDataBookCursor *cursor)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), -1);

	return cursor->priv->position;
}


/**
 * e_data_book_cursor_set_sexp:
 * @cursor: an #EDataBookCursor
 * @sexp: (allow-none): the search expression to set
 * @error: (out) (allow-none): return location for a #GError, or %NULL
 *
 * Sets the search expression for the cursor
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set.
 *
 * Since: 3.12
 */
gboolean
e_data_book_cursor_set_sexp (EDataBookCursor     *cursor,
			     const gchar         *sexp,
			     GError             **error)
{
	GError *local_error = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), FALSE);

	g_object_ref (cursor);

	if (E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->set_sexp) {
		success = (* E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->set_sexp) (cursor,
									       sexp,
									       error);

	} else {
		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_NOT_SUPPORTED,
				     _("Cursor does not support setting the search expression"));
	}

	/* We already set the new search expression,
	 * we can't fail anymore so just fire a warning
	 */
	if (success &&
	    !e_data_book_cursor_recalculate (cursor, &local_error)) {
		g_warning ("Failed to recalculate the cursor value "
			   "after setting the search expression: %s",
			   local_error->message);
		g_clear_error (&local_error);
	}

	g_object_unref (cursor);

	return success;
}

/**
 * e_data_book_cursor_move_by:
 * @cursor: an #EDataBookCursor
 * @revision_guard: The expected current addressbook revision, or %NULL
 * @origin: the #EBookCursorOrigin for this move
 * @count: a positive or negative amount of contacts to try and fetch
 * @results: (out) (allow-none) (element-type utf8) (transfer full):
 *   A return location to store the results, or %NULL to move the cursor without retrieving any results.
 * @error: (out) (allow-none): return location for a #GError, or %NULL
 *
 * Moves @cursor through the results by @count and fetch a maximum of @count contacts.
 *
 * If @count is negative, then the cursor will move backwards.
 *
 * If @cursor is in an empty state, or @origin is %E_BOOK_CURSOR_ORIGIN_RESET,
 * then @count contacts will be fetched from the beginning of the cursor's query
 * results, or from the ending of the query results for a negative value of @count.
 *
 * If @cursor reaches the beginning or end of the query results, then the
 * returned list might not contain the amount of desired contacts, or might
 * return no results if the cursor currently points to the last contact.
 * This is not considered an error condition.
 *
 * If @results is specified, it should be a pointer to a %NULL #GSList,
 * the result list will be stored to @results and should be freed with g_slist_free()
 * and all elements freed with g_free().
 *
 * If a @revision_guard is specified, the cursor implementation will issue an
 * %E_CLIENT_ERROR_OUT_OF_SYNC error if the @revision_guard does not match
 * the current addressbook revision.
 *
 * Returns: The number of contacts which the cursor has moved by if successfull.
 * Otherwise -1 is returned and @error is set.
 *
 * Since: 3.12
 */
gint
e_data_book_cursor_move_by (EDataBookCursor     *cursor,
			    const gchar         *revision_guard,
			    EBookCursorOrigin    origin,
			    gint                 count,
			    GSList             **results,
			    GError             **error)
{
	gint retval;

	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), FALSE);

	if (!E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->move_by) {
		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_NOT_SUPPORTED,
				     _("Cursor does not support moves"));
		return FALSE;
	}

	g_object_ref (cursor);
	retval = (* E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->move_by) (cursor,
								      revision_guard,
								      origin,
								      count,
								      results,
								      error);
	g_object_unref (cursor);

	if (retval > 0) {
		/* Calculate new cursor position and notify change */
		calculate_move_by_position (cursor, origin, count, retval);
	}

	return retval;
}

/**
 * e_data_book_cursor_set_alphabetic_index:
 * @cursor: an #EDataBookCursor
 * @index: the alphabetic index
 * @locale: the locale in which @index is expected to be a valid alphabetic index
 * @error: (out) (allow-none): return location for a #GError, or %NULL
 *
 * Sets the @cursor position to an
 * <link linkend="cursor-alphabet">Alphabetic Index</link>
 * into the alphabet active in the @locale of the addressbook.
 *
 * After setting the target to an alphabetic index, for example the
 * index for letter 'E', then further calls to e_data_book_cursor_move_by()
 * will return results starting with the letter 'E' (or results starting
 * with the last result in 'D', if moving in a negative direction).
 *
 * The passed index must be a valid index in @locale, if by some chance
 * the addressbook backend has changed into a new locale after this
 * call has been issued, an %E_CLIENT_ERROR_OUT_OF_SYNC error will be
 * issued indicating that there was a locale mismatch.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set.
 *
 * Since: 3.12
 */
gboolean
e_data_book_cursor_set_alphabetic_index (EDataBookCursor     *cursor,
					 gint                 index,
					 const gchar         *locale,
					 GError             **error)
{
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), FALSE);

	g_object_ref (cursor);

	if (E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->set_alphabetic_index) {
		success = (* E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->set_alphabetic_index) (cursor,
											   index,
											   locale,
											   error);

		/* We already set the new cursor value, we can't fail anymore so just fire a warning */
		if (!e_data_book_cursor_recalculate (cursor, &local_error)) {
			g_warning ("Failed to recalculate the cursor value "
				   "after setting the alphabetic index: %s",
				   local_error->message);
			g_clear_error (&local_error);
		}

	} else {
		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_NOT_SUPPORTED,
				     _("Cursor does not support alphabetic indexes"));
		success = FALSE;
	}

	g_object_unref (cursor);

	return success;
}

/**
 * e_data_book_cursor_recalculate:
 * @cursor: an #EDataBookCursor
 * @error: (out) (allow-none): return location for a #GError, or %NULL
 *
 * Recalculates the cursor's total and position, this is meant
 * for cursor created in Direct Read Access mode to synchronously
 * recalculate the position and total values when the addressbook
 * revision has changed.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set.
 *
 * Since: 3.12
 */
gboolean
e_data_book_cursor_recalculate (EDataBookCursor     *cursor,
				GError             **error)
{
	gint total = 0;
	gint position = 0;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), FALSE);

	/* Bad programming error */
	if (!E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->get_position) {
		g_critical ("EDataBookCursor.get_position() unimplemented on type '%s'",
			    G_OBJECT_TYPE_NAME (cursor));

		return FALSE;
	}

	g_object_ref (cursor);
	success =  (* E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->get_position) (cursor,
									    &total,
									    &position,
									    error);
	g_object_unref (cursor);

	if (success)
		data_book_cursor_set_values (cursor, total, position);

	return success;
}

/**
 * e_data_book_cursor_load_locale:
 * @cursor: an #EDataBookCursor
 * @locale: (out) (allow-none): return location for the locale
 * @error: (out) (allow-none): return location for a #GError, or %NULL
 *
 * Load the current locale setting from the cursor's underlying database.
 *
 * Addressbook backends implementing cursors should call this function on all active
 * cursor when the locale setting changes.
 *
 * This will implicitly reset @cursor's state and position.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set.
 *
 * Since: 3.12
 */
gboolean
e_data_book_cursor_load_locale (EDataBookCursor     *cursor,
				gchar              **locale,
				GError             **error)
{
	EDataBookCursorPrivate *priv;
	gboolean success;
	gchar *local_locale = NULL;

	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), FALSE);

	priv = cursor->priv;

	if (!E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->load_locale) {
		g_critical ("EDataBookCursor.load_locale() unimplemented on type '%s'",
			    G_OBJECT_TYPE_NAME (cursor));
		return FALSE;
	}

	g_object_ref (cursor);
	success = (* E_DATA_BOOK_CURSOR_GET_CLASS (cursor)->load_locale) (cursor, &local_locale, error);
	g_object_unref (cursor);

	/* Changed ! Reset the thing */
	if (g_strcmp0 (priv->locale, local_locale) != 0) {
		GError *local_error = NULL;

		g_free (priv->locale);
		priv->locale = g_strdup (local_locale);

		if (e_data_book_cursor_move_by (cursor, NULL,
						E_BOOK_CURSOR_ORIGIN_RESET,
						0, NULL, &local_error) < 0) {
			g_warning ("Error resetting cursor position after locale change: %s",
				   local_error->message);
			g_clear_error (&local_error);
		} else if (!e_data_book_cursor_recalculate (E_DATA_BOOK_CURSOR (cursor),
							    &local_error)) {
			g_warning ("Error recalculating cursor position after locale change: %s",
				   local_error->message);
			g_clear_error (&local_error);
		}
	}

	if (locale)
		*locale = local_locale;
	else
		g_free (local_locale);

	return success;
}

/**
 * e_data_book_cursor_contact_added:
 * @cursor: an #EDataBookCursor
 * @contact: the #EContact which was added to the addressbook
 *
 * Should be called by addressbook backends whenever a contact
 * is added.
 *
 * Since: 3.12
 */
void
e_data_book_cursor_contact_added (EDataBookCursor     *cursor,
				  EContact            *contact)
{
	EDataBookCursorPrivate *priv;
	gint comparison = 0;
	gboolean matches_sexp = FALSE;
	gint new_total, new_position;

	g_return_if_fail (E_IS_DATA_BOOK_CURSOR (cursor));
	g_return_if_fail (E_IS_CONTACT (contact));

	priv = cursor->priv;

	if (!data_book_cursor_compare_contact (cursor, contact, &comparison, &matches_sexp)) {
		GError *error = NULL;

		/* Comparisons not supported, must recalculate entirely */
		if (!e_data_book_cursor_recalculate (cursor, &error)) {
			g_warning ("Failed to recalculate the cursor value "
				   "after a contact was added: %s",
				   error->message);
			g_clear_error (&error);
		}

		return;
	}

	/* The added contact doesn't match the cursor search expression, no need
	 * to change the position & total values
	 */
	if (!matches_sexp)
		return;

	new_total = priv->total;
	new_position = priv->position;

	/* One new contact */
	new_total++;

	/* New contact was added at cursor position or before cursor position */
	if (comparison <= 0)
		new_position++;

	/* Notify total & position change */
	data_book_cursor_set_values (cursor, new_total, new_position);
}

/**
 * e_data_book_cursor_contact_removed:
 * @cursor: an #EDataBookCursor
 * @contact: the #EContact which was removed from the addressbook
 *
 * Should be called by addressbook backends whenever a contact
 * is removed.
 *
 * Since: 3.12
 */
void
e_data_book_cursor_contact_removed (EDataBookCursor     *cursor,
				    EContact            *contact)
{
	EDataBookCursorPrivate *priv;
	gint comparison = 0;
	gboolean matches_sexp = FALSE;
	gint new_total, new_position;

	g_return_if_fail (E_IS_DATA_BOOK_CURSOR (cursor));
	g_return_if_fail (E_IS_CONTACT (contact));

	priv = cursor->priv;

	if (!data_book_cursor_compare_contact (cursor, contact, &comparison, &matches_sexp)) {
		GError *error = NULL;

		/* Comparisons not supported, must recalculate entirely */
		if (!e_data_book_cursor_recalculate (cursor, &error)) {
			g_warning ("Failed to recalculate the cursor value "
				   "after a contact was added: %s",
				   error->message);
			g_clear_error (&error);
		}

		return;
	}

	/* The removed contact did not match the cursor search expression, no need
	 * to change the position & total values
	 */
	if (!matches_sexp)
		return;

	new_total = priv->total;
	new_position = priv->position;

	/* One less contact */
	new_total--;

	/* Removed contact was the exact cursor position or before cursor position */
	if (comparison <= 0)
		new_position--;

	/* Notify total & position change */
	data_book_cursor_set_values (cursor, new_total, new_position);
}

/**
 * e_data_book_cursor_register_gdbus_object:
 * @cursor: an #EDataBookCursor
 * @connection: the #GDBusConnection to register with
 * @object_path: the object path to place the direct access configuration data
 * @error: (out) (allow-none): a location to store any error which might occur while registering
 *
 * Places @cursor on the @connection at @object_path
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set.
 *
 * Since: 3.12
 */
gboolean
e_data_book_cursor_register_gdbus_object (EDataBookCursor     *cursor,
					  GDBusConnection     *connection,
					  const gchar         *object_path,
					  GError             **error)
{
	EDataBookCursorPrivate *priv;

	g_return_val_if_fail (E_IS_DATA_BOOK_CURSOR (cursor), FALSE);
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);
	g_return_val_if_fail (object_path != NULL, FALSE);

	priv = cursor->priv;

	if (!priv->dbus_object) {
		priv->dbus_object = e_dbus_address_book_cursor_skeleton_new ();

		g_signal_connect (priv->dbus_object, "handle-move-by",
				  G_CALLBACK (data_book_cursor_handle_move_by), cursor);
		g_signal_connect (priv->dbus_object, "handle-set-alphabetic-index",
				  G_CALLBACK (data_book_cursor_handle_set_alphabetic_index), cursor);
		g_signal_connect (priv->dbus_object, "handle-set-query",
				  G_CALLBACK (data_book_cursor_handle_set_query), cursor);
		g_signal_connect (priv->dbus_object, "handle-dispose",
				  G_CALLBACK (data_book_cursor_handle_dispose), cursor);


		/* Set initial total / position */
		e_dbus_address_book_cursor_set_total (priv->dbus_object, priv->total);
		e_dbus_address_book_cursor_set_position (priv->dbus_object, priv->position);
	}

	return g_dbus_interface_skeleton_export (
		G_DBUS_INTERFACE_SKELETON (priv->dbus_object),
		connection, object_path, error);
}

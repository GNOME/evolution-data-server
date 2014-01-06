/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* camelMedium.c : Abstract class for a medium
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>

#include "camel-medium.h"

#define d(x)

#define CAMEL_MEDIUM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_MEDIUM, CamelMediumPrivate))

struct _CamelMediumPrivate {
	/* The content of the medium, as opposed to our parent
	 * CamelDataWrapper, which wraps both the headers and
	 * the content. */
	CamelDataWrapper *content;
};

enum {
	PROP_0,
	PROP_CONTENT
};

G_DEFINE_ABSTRACT_TYPE (CamelMedium, camel_medium, CAMEL_TYPE_DATA_WRAPPER)

static void
medium_set_property (GObject *object,
                     guint property_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTENT:
			camel_medium_set_content (
				CAMEL_MEDIUM (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
medium_get_property (GObject *object,
                     guint property_id,
                     GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTENT:
			g_value_set_object (
				value, camel_medium_get_content (
				CAMEL_MEDIUM (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
medium_dispose (GObject *object)
{
	CamelMediumPrivate *priv;

	priv = CAMEL_MEDIUM_GET_PRIVATE (object);

	if (priv->content != NULL) {
		g_object_unref (priv->content);
		priv->content = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_medium_parent_class)->dispose (object);
}

static gboolean
medium_is_offline (CamelDataWrapper *data_wrapper)
{
	CamelDataWrapper *content;

	content = camel_medium_get_content (CAMEL_MEDIUM (data_wrapper));

	return CAMEL_DATA_WRAPPER_CLASS (camel_medium_parent_class)->is_offline (data_wrapper) ||
		camel_data_wrapper_is_offline (content);
}

static void
medium_set_content (CamelMedium *medium,
                    CamelDataWrapper *content)
{
	if (medium->priv->content == content)
		return;

	if (content != NULL)
		g_object_ref (content);

	if (medium->priv->content != NULL)
		g_object_unref (medium->priv->content);

	medium->priv->content = content;

	g_object_notify (G_OBJECT (medium), "content");
}

static CamelDataWrapper *
medium_get_content (CamelMedium *medium)
{
	return medium->priv->content;
}

static void
camel_medium_class_init (CamelMediumClass *class)
{
	GObjectClass *object_class;
	CamelDataWrapperClass *data_wrapper_class;

	g_type_class_add_private (class, sizeof (CamelMediumPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = medium_set_property;
	object_class->get_property = medium_get_property;
	object_class->dispose = medium_dispose;

	data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (class);
	data_wrapper_class->is_offline = medium_is_offline;

	class->set_content = medium_set_content;
	class->get_content = medium_get_content;

	g_object_class_install_property (
		object_class,
		PROP_CONTENT,
		g_param_spec_object (
			"content",
			"Content",
			NULL,
			CAMEL_TYPE_DATA_WRAPPER,
			G_PARAM_READWRITE));
}

static void
camel_medium_init (CamelMedium *medium)
{
	medium->priv = CAMEL_MEDIUM_GET_PRIVATE (medium);
}

/**
 * camel_medium_add_header:
 * @medium: a #CamelMedium object
 * @name: name of the header
 * @value: value of the header
 *
 * Adds a header to a #CamelMedium.
 **/
void
camel_medium_add_header (CamelMedium *medium,
                         const gchar *name,
                         gconstpointer value)
{
	CamelMediumClass *class;

	g_return_if_fail (CAMEL_IS_MEDIUM (medium));
	g_return_if_fail (name != NULL);
	g_return_if_fail (value != NULL);

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_if_fail (class->add_header != NULL);

	class->add_header (medium, name, value);
}

/**
 * camel_medium_set_header:
 * @medium: a #CamelMedium object
 * @name: name of the header
 * @value: value of the header
 *
 * Sets the value of a header.  Any other occurances of the header
 * will be removed.  Setting a %NULL header can be used to remove
 * the header also.
 **/
void
camel_medium_set_header (CamelMedium *medium,
                         const gchar *name,
                         gconstpointer value)
{
	CamelMediumClass *class;

	g_return_if_fail (CAMEL_IS_MEDIUM (medium));
	g_return_if_fail (name != NULL);

	if (value == NULL) {
		camel_medium_remove_header (medium, name);
		return;
	}

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_if_fail (class->set_header != NULL);

	class->set_header (medium, name, value);
}

/**
 * camel_medium_remove_header:
 * @medium: a #CamelMedium
 * @name: the name of the header
 *
 * Removes the named header from the medium.  All occurances of the
 * header are removed.
 **/
void
camel_medium_remove_header (CamelMedium *medium,
                            const gchar *name)
{
	CamelMediumClass *class;

	g_return_if_fail (CAMEL_IS_MEDIUM (medium));
	g_return_if_fail (name != NULL);

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_if_fail (class->remove_header != NULL);

	class->remove_header (medium, name);
}

/**
 * camel_medium_get_header:
 * @medium: a #CamelMedium
 * @name: the name of the header
 *
 * Gets the value of the named header in the medium, or %NULL if
 * it is unset. The caller should not modify or free the data.
 *
 * If the header occurs more than once, only retrieve the first
 * instance of the header.  For multi-occuring headers, use
 * :get_headers().
 *
 * Returns: the value of the named header, or %NULL
 **/
gconstpointer
camel_medium_get_header (CamelMedium *medium,
                         const gchar *name)
{
	CamelMediumClass *class;

	g_return_val_if_fail (CAMEL_IS_MEDIUM (medium), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_val_if_fail (class->get_header != NULL, NULL);

	return class->get_header (medium, name);
}

/**
 * camel_medium_get_headers:
 * @medium: a #CamelMedium object
 *
 * Gets an array of all header name/value pairs (as
 * CamelMediumHeader structures). The values will be decoded
 * to UTF-8 for any headers that are recognized by Camel. The
 * caller should not modify the returned data.
 *
 * Returns: the array of headers, which must be freed with
 * camel_medium_free_headers().
 **/
GArray *
camel_medium_get_headers (CamelMedium *medium)
{
	CamelMediumClass *class;

	g_return_val_if_fail (CAMEL_IS_MEDIUM (medium), NULL);

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_val_if_fail (class->get_headers != NULL, NULL);

	return class->get_headers (medium);
}

/**
 * camel_medium_free_headers:
 * @medium: a #CamelMedium object
 * @headers: an array of headers returned from camel_medium_get_headers()
 *
 * Frees @headers.
 **/
void
camel_medium_free_headers (CamelMedium *medium,
                           GArray *headers)
{
	CamelMediumClass *class;

	g_return_if_fail (CAMEL_IS_MEDIUM (medium));
	g_return_if_fail (headers != NULL);

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_if_fail (class->free_headers != NULL);

	class->free_headers (medium, headers);
}

/**
 * camel_medium_get_content:
 * @medium: a #CamelMedium object
 *
 * Gets a data wrapper that represents the content of the medium,
 * without its headers.
 *
 * Returns: a #CamelDataWrapper containing @medium's content. Can return NULL.
 **/
CamelDataWrapper *
camel_medium_get_content (CamelMedium *medium)
{
	CamelMediumClass *class;

	g_return_val_if_fail (CAMEL_IS_MEDIUM (medium), NULL);

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_val_if_fail (class->get_content != NULL, NULL);

	return class->get_content (medium);
}

/**
 * camel_medium_set_content:
 * @medium: a #CamelMedium object
 * @content: a #CamelDataWrapper object
 *
 * Sets the content of @medium to be @content.
 **/
void
camel_medium_set_content (CamelMedium *medium,
                          CamelDataWrapper *content)
{
	CamelMediumClass *class;

	g_return_if_fail (CAMEL_IS_MEDIUM (medium));

	if (content != NULL)
		g_return_if_fail (CAMEL_IS_DATA_WRAPPER (content));

	class = CAMEL_MEDIUM_GET_CLASS (medium);
	g_return_if_fail (class->set_content != NULL);

	class->set_content (medium, content);
}

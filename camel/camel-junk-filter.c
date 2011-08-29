/*
 * camel-junk-filter.c
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
 */

#include "camel-junk-filter.h"

G_DEFINE_INTERFACE (CamelJunkFilter, camel_junk_filter, G_TYPE_OBJECT)

static void
camel_junk_filter_default_init (CamelJunkFilterInterface *interface)
{
}

/**
 * camel_junk_filter_classify:
 * @junk_filter: a #CamelJunkFilter
 * @message: a #CamelMimeMessage
 * @status: location to write the #CamelJunkStatus
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Classifies @message as junk, not junk or inconclusive.
 *
 * If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE if @message was successfully classified
 *
 * Since: 3.2
 **/
gboolean
camel_junk_filter_classify (CamelJunkFilter *junk_filter,
                            CamelMimeMessage *message,
                            CamelJunkStatus *status,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelJunkFilterInterface *interface;

	g_return_val_if_fail (CAMEL_IS_JUNK_FILTER (junk_filter), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);
	g_return_val_if_fail (status != NULL, FALSE);

	interface = CAMEL_JUNK_FILTER_GET_INTERFACE (junk_filter);
	g_return_val_if_fail (interface->classify != NULL, FALSE);

	return interface->classify (
		junk_filter, message, status, cancellable, error);
}

/**
 * camel_junk_filter_learn_junk:
 * @junk_filter: a #CamelJunkFilter
 * @message: a #CamelMimeMessage
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Instructs @junk_filter to classify @message as junk.  If using an
 * adaptive junk filtering algorithm, explicitly marking @message as
 * junk will influence the classification of future messages.
 *
 * If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE if @message was successfully classified
 *
 * Since: 3.2
 **/
gboolean
camel_junk_filter_learn_junk (CamelJunkFilter *junk_filter,
                              CamelMimeMessage *message,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelJunkFilterInterface *interface;

	g_return_val_if_fail (CAMEL_IS_JUNK_FILTER (junk_filter), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);

	interface = CAMEL_JUNK_FILTER_GET_INTERFACE (junk_filter);
	g_return_val_if_fail (interface->learn_junk != NULL, FALSE);

	return interface->learn_junk (
		junk_filter, message, cancellable, error);
}

/**
 * camel_junk_filter_learn_not_junk:
 * @junk_filter: a #CamelJunkFilter
 * @message: a #CamelMimeMessage
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Instructs @junk_filter to classify @message as not junk.  If using an
 * adaptive junk filtering algorithm, explicitly marking @message as not
 * junk will influence the classification of future messages.
 *
 * If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE if @message was successfully classified
 *
 * Since: 3.2
 **/
gboolean
camel_junk_filter_learn_not_junk (CamelJunkFilter *junk_filter,
                                  CamelMimeMessage *message,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelJunkFilterInterface *interface;

	g_return_val_if_fail (CAMEL_IS_JUNK_FILTER (junk_filter), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);

	interface = CAMEL_JUNK_FILTER_GET_INTERFACE (junk_filter);
	g_return_val_if_fail (interface->learn_not_junk != NULL, FALSE);

	return interface->learn_not_junk (
		junk_filter, message, cancellable, error);
}

/**
 * camel_junk_filter_synchronize:
 * @junk_filter: a #CamelJunkFilter
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Instructs @junk_filter to flush any in-memory caches to disk, if
 * applicable.  When filtering many messages, delaying this step until
 * all messages have been classified can improve performance.
 *
 * If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE if @junk_filter was successfully synchronized
 *
 * Since: 3.2
 **/
gboolean
camel_junk_filter_synchronize (CamelJunkFilter *junk_filter,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelJunkFilterInterface *interface;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_JUNK_FILTER (junk_filter), FALSE);

	/* This method is optional. */
	interface = CAMEL_JUNK_FILTER_GET_INTERFACE (junk_filter);

	if (interface->synchronize != NULL)
		success = interface->synchronize (
			junk_filter, cancellable, error);

	return success;
}


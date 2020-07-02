/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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

#include <stdio.h>
#include <libsoup/soup.h>

#include "e-soup-logger.h"

/* Standard GObject macros */
#define E_TYPE_SOUP_LOGGER \
	(e_soup_logger_get_type ())
#define E_SOUP_LOGGER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOUP_LOGGER, EO365SoupLogger))
#define E_SOUP_LOGGER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOUP_LOGGER, EO365SoupLoggerClass))
#define E_IS_SOUP_LOGGER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOUP_LOGGER))
#define E_IS_SOUP_LOGGER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOUP_LOGGER))
#define E_SOUP_LOGGER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOUP_LOGGER))

G_BEGIN_DECLS

typedef struct _EO365SoupLogger EO365SoupLogger;
typedef struct _EO365SoupLoggerClass EO365SoupLoggerClass;

struct _EO365SoupLogger {
	GObject parent;

	GByteArray *data;
};

struct _EO365SoupLoggerClass {
	GObjectClass parent_class;
};

GType		e_soup_logger_get_type	(void) G_GNUC_CONST;

static void	e_soup_logger_converter_interface_init
						(GConverterIface *iface);

G_DEFINE_TYPE_WITH_CODE (EO365SoupLogger, e_soup_logger, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (G_TYPE_CONVERTER, e_soup_logger_converter_interface_init))

static GConverterResult
e_soup_logger_convert (GConverter *converter,
		       gconstpointer inbuf,
		       gsize inbuf_size,
		       gpointer outbuf,
		       gsize outbuf_size,
		       GConverterFlags flags,
		       gsize *bytes_read,
		       gsize *bytes_written,
		       GError **error)
{
	EO365SoupLogger *logger = E_SOUP_LOGGER (converter);
	GConverterResult result;
	gsize min_size;

	min_size = MIN (inbuf_size, outbuf_size);

	if (inbuf && min_size)
		memcpy (outbuf, inbuf, min_size);
	*bytes_read = *bytes_written = min_size;

	if (!logger->data)
		logger->data = g_byte_array_sized_new (10240);

	g_byte_array_append (logger->data, (const guint8 *) outbuf, (guint) min_size);

	if ((flags & G_CONVERTER_INPUT_AT_END) != 0)
		result = G_CONVERTER_FINISHED;
	else if ((flags & G_CONVERTER_FLUSH) != 0)
		result = G_CONVERTER_FLUSHED;
	else
		result = G_CONVERTER_CONVERTED;

	return result;
}

static void
e_soup_logger_reset (GConverter *converter)
{
	/* Nothing to do. */
}

static void
e_soup_logger_print_data (EO365SoupLogger *logger)
{
	if (logger->data) {
		fwrite (logger->data->data, 1, logger->data->len, stdout);
		fwrite ("\n\n", 1, 2, stdout);

		g_byte_array_free (logger->data, TRUE);
		logger->data = NULL;
	}

	fflush (stdout);
}

static void
e_soup_logger_message_finished_cb (SoupMessage *msg,
					gpointer user_data)
{
	EO365SoupLogger *logger = user_data;

	g_return_if_fail (E_IS_SOUP_LOGGER (logger));

	e_soup_logger_print_data (logger);
}

static void
o365_soup_logger_finalize (GObject *object)
{
	EO365SoupLogger *logger = E_SOUP_LOGGER (object);

	e_soup_logger_print_data (logger);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_soup_logger_parent_class)->finalize (object);
}

static void
e_soup_logger_class_init (EO365SoupLoggerClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = o365_soup_logger_finalize;
}

static void
e_soup_logger_converter_interface_init (GConverterIface *iface)
{
	iface->convert = e_soup_logger_convert;
	iface->reset = e_soup_logger_reset;
}

static void
e_soup_logger_init (EO365SoupLogger *logger)
{
}

/**
 * e_soup_logger_attach:
 * @message: a #SoupMessage
 * @input_stream: (transfer full): a #GInputStream, associated with the @message
 *
 * Remembers what had been read from the @input_stream and prints it
 * to stdout when the @message is finished. The function assumes
 * ownership of the @input_stream.
 *
 * Returns: (transfer full): a new input stream, to be used instead of the @input_stream.
 *    It should be freed with g_object_unref(), when no longer needed.
 *
 * Since: 3.38
 **/
GInputStream *
e_soup_logger_attach (SoupMessage *message,
		      GInputStream *input_stream)
{
	GConverter *logger;
	GInputStream *filter_stream;

	g_return_val_if_fail (SOUP_IS_MESSAGE (message), input_stream);
	g_return_val_if_fail (G_IS_INPUT_STREAM (input_stream), input_stream);

	logger = g_object_new (E_TYPE_SOUP_LOGGER, NULL);

	filter_stream = g_converter_input_stream_new (input_stream, logger);
	g_object_set_data_full (G_OBJECT (message), "ESoupLogger", logger, g_object_unref);

	g_signal_connect_object (message, "finished",
		G_CALLBACK (e_soup_logger_message_finished_cb), logger, G_CONNECT_AFTER);

	g_object_unref (input_stream);

	return filter_stream;
}

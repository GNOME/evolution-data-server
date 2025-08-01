/*
 * camel-imapx-logger.c
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
 * SECTION: camel-imapx-logger
 * @include: camel-imapx-logger.h
 * @short_description: Log input/output streams
 *
 * #CamelIMAPXLogger is a simple #GConverter that just echos data to standard
 * output if the I/O debugging setting is enabled ('CAMEL_DEBUG=imapx:io').
 * Attaches to the #GInputStream and #GOutputStream.
 **/

#include "evolution-data-server-config.h"

#include "camel-imapx-logger.h"

#include <string.h>

#include "camel-imapx-utils.h"

struct _CamelIMAPXLoggerPrivate {
	gchar prefix;
	GWeakRef server_weakref;
};

enum {
	PROP_0,
	PROP_PREFIX,
	PROP_SERVER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

/* Forward Declarations */
static void	camel_imapx_logger_interface_init
						(GConverterIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	CamelIMAPXLogger,
	camel_imapx_logger,
	G_TYPE_OBJECT,
	G_ADD_PRIVATE (CamelIMAPXLogger)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_CONVERTER,
		camel_imapx_logger_interface_init))

static void
imapx_logger_set_prefix (CamelIMAPXLogger *logger,
                         gchar prefix)
{
	logger->priv->prefix = prefix;
}

static void
imapx_logger_set_server (CamelIMAPXLogger *logger,
			 CamelIMAPXServer *server)
{
	g_weak_ref_set (&logger->priv->server_weakref, server);
}

static CamelIMAPXServer *
imapx_logger_dup_server (CamelIMAPXLogger *logger)
{
	return g_weak_ref_get (&logger->priv->server_weakref);
}

static void
imapx_logger_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PREFIX:
			imapx_logger_set_prefix (
				CAMEL_IMAPX_LOGGER (object),
				g_value_get_schar (value));
			return;
		case PROP_SERVER:
			imapx_logger_set_server (
				CAMEL_IMAPX_LOGGER (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_logger_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PREFIX:
			g_value_set_schar (
				value,
				camel_imapx_logger_get_prefix (
				CAMEL_IMAPX_LOGGER (object)));
			return;
		case PROP_SERVER:
			g_value_take_object (
				value,
				imapx_logger_dup_server (
				CAMEL_IMAPX_LOGGER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_logger_finalize (GObject *object)
{
	CamelIMAPXLogger *self = CAMEL_IMAPX_LOGGER (object);

	g_weak_ref_clear (&self->priv->server_weakref);

	G_OBJECT_CLASS (camel_imapx_logger_parent_class)->finalize (object);
}

static gboolean
imapx_logger_discard_logging (CamelIMAPXLogger *logger,
			      const gchar **out_replace_text)
{
	CamelIMAPXServer *server;
	gboolean res;

	server = imapx_logger_dup_server (logger);
	if (!server)
		return FALSE;

	res = camel_imapx_server_should_discard_logging (server, out_replace_text);

	g_object_unref (server);

	return res;
}

static GConverterResult
imapx_logger_convert (GConverter *converter,
                      gconstpointer inbuf,
                      gsize inbuf_size,
                      gpointer outbuf,
                      gsize outbuf_size,
                      GConverterFlags flags,
                      gsize *bytes_read,
                      gsize *bytes_written,
                      GError **error)
{
	CamelIMAPXLogger *logger;
	GConverterResult result;
	gsize min_size;
	const gchar *replace_text = NULL;

	logger = CAMEL_IMAPX_LOGGER (converter);

	min_size = MIN (inbuf_size, outbuf_size);

	if (inbuf && min_size)
		memcpy (outbuf, inbuf, min_size);
	*bytes_read = *bytes_written = min_size;

	if (imapx_logger_discard_logging (logger, &replace_text)) {
		camel_imapx_debug (
			io, logger->priv->prefix, "I/O: %s...\n",
			replace_text ? replace_text : "");
	} else {
		/* Skip ending '\n' '\r'; it may sometimes show wrong data,
		   when the input is divided into wrong chunks, but it will
		   usually work as is needed, no extra new-lines in the log */
		while (min_size > 0 && (((gchar *) outbuf)[min_size - 1] == '\r' || ((gchar *) outbuf)[min_size - 1] == '\n'))
			min_size--;

		camel_imapx_debug (
			io, logger->priv->prefix, "I/O: '%.*s'\n",
			(gint) min_size, (gchar *) outbuf);
	}

	if ((flags & G_CONVERTER_INPUT_AT_END) != 0)
		result = G_CONVERTER_FINISHED;
	else if ((flags & G_CONVERTER_FLUSH) != 0)
		result = G_CONVERTER_FLUSHED;
	else
		result = G_CONVERTER_CONVERTED;

	return result;
}

static void
imapx_logger_reset (GConverter *converter)
{
	/* Nothing to do. */
}

static void
camel_imapx_logger_class_init (CamelIMAPXLoggerClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_logger_set_property;
	object_class->get_property = imapx_logger_get_property;
	object_class->finalize = imapx_logger_finalize;

	/**
	 * CamelIMAPXLogger:prefix
	 *
	 * Output prefix to distinguish connections
	 **/
	properties[PROP_PREFIX] =
		g_param_spec_char (
			"prefix", NULL, NULL,
			0x20, 0x7F, '*',
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXLogger:server
	 *
	 * The #CamelIMAPXServer
	 **/
	properties[PROP_SERVER] =
		g_param_spec_object (
			"server", NULL, NULL,
			CAMEL_TYPE_IMAPX_SERVER,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
camel_imapx_logger_interface_init (GConverterIface *iface)
{
	iface->convert = imapx_logger_convert;
	iface->reset = imapx_logger_reset;
}

static void
camel_imapx_logger_init (CamelIMAPXLogger *logger)
{
	logger->priv = camel_imapx_logger_get_instance_private (logger);
	g_weak_ref_init (&logger->priv->server_weakref, NULL);
}

/**
 * camel_imapx_logger_new:
 * @prefix: a prefix character
 * @server: (nullable): a CamelIMAPXServer, or %NULL
 *
 * Creates a new #CamelIMAPXLogger.  Each output line generated by the
 * logger will have a prefix string that includes the @prefix character
 * to distinguish it from other active loggers.
 *
 * The @server can hint to discard logging for certain commands.
 *
 * Returns: a #CamelIMAPXLogger
 *
 * Since: 3.12
 **/
GConverter *
camel_imapx_logger_new (gchar prefix,
			CamelIMAPXServer *server)
{
	return g_object_new (CAMEL_TYPE_IMAPX_LOGGER,
		"prefix", prefix,
		"server", server,
		NULL);
}

/**
 * camel_imapx_logger_get_prefix:
 * @logger: a #CamelIMAPXLogger
 *
 * Returns the prefix character passed to camel_imapx_logger_new().
 *
 * Returns: the prefix character
 *
 * Since: 3.12
 **/
gchar
camel_imapx_logger_get_prefix (CamelIMAPXLogger *logger)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_LOGGER (logger), 0);

	return logger->priv->prefix;
}


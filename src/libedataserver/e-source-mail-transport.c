/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-source-mail-transport
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for an email transport
 *
 * The #ESourceMailTransport extension identifies the #ESource as a
 * mail transport which describes where to send outgoing messages.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceMailTransport *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
 * ]|
 **/

#include "e-source-mail-transport.h"

G_DEFINE_TYPE (
	ESourceMailTransport,
	e_source_mail_transport,
	E_TYPE_SOURCE_BACKEND)

static void
e_source_mail_transport_class_init (ESourceMailTransportClass *class)
{
	ESourceExtensionClass *extension_class;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
}

static void
e_source_mail_transport_init (ESourceMailTransport *extension)
{
}


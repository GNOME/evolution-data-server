/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_JMAP_TRANSPORT_H
#define CAMEL_JMAP_TRANSPORT_H

#include <camel/camel.h>

G_BEGIN_DECLS

#define CAMEL_TYPE_JMAP_TRANSPORT camel_jmap_transport_get_type ()
G_DECLARE_FINAL_TYPE (CamelJmapTransport, camel_jmap_transport, CAMEL, JMAP_TRANSPORT, CamelTransport)

G_END_DECLS

#endif /* CAMEL_JMAP_TRANSPORT_H */

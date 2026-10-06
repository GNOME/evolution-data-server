/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_XOAUTH2_DYNAMIC_H
#define CAMEL_SASL_XOAUTH2_DYNAMIC_H

#include <camel/camel-sasl-xoauth2.h>

G_BEGIN_DECLS

#define CAMEL_TYPE_SASL_XOAUTH2_DYNAMIC (camel_sasl_xoauth2_dynamic_get_type ())
G_DECLARE_FINAL_TYPE (CamelSaslXOAuth2Dynamic, camel_sasl_xoauth2_dynamic, CAMEL, SASL_XOAUTH2_DYNAMIC, CamelSaslXOAuth2)

G_END_DECLS

#endif /* CAMEL_SASL_XOAUTH2_DYNAMIC_H */

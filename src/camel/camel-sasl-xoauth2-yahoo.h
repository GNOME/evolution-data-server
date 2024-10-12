/*
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_XOAUTH2_YAHOO_H
#define CAMEL_SASL_XOAUTH2_YAHOO_H

#include <camel/camel-sasl-xoauth2.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_XOAUTH2_YAHOO \
	(camel_sasl_xoauth2_yahoo_get_type ())
#define CAMEL_SASL_XOAUTH2_YAHOO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_YAHOO, CamelSaslXOAuth2Yahoo))
#define CAMEL_SASL_XOAUTH2_YAHOO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_XOAUTH2_YAHOO, CamelSaslXOAuth2YahooClass))
#define CAMEL_IS_SASL_XOAUTH2_YAHOO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_YAHOO))
#define CAMEL_IS_SASL_XOAUTH2_YAHOO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_XOAUTH2_YAHOO))
#define CAMEL_SASL_XOAUTH2_YAHOO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_YAHOO, CamelSaslXOAuth2YahooClass))

G_BEGIN_DECLS

typedef struct _CamelSaslXOAuth2Yahoo CamelSaslXOAuth2Yahoo;
typedef struct _CamelSaslXOAuth2YahooClass CamelSaslXOAuth2YahooClass;
typedef struct _CamelSaslXOAuth2YahooPrivate CamelSaslXOAuth2YahooPrivate;

struct _CamelSaslXOAuth2Yahoo {
	CamelSaslXOAuth2 parent;
	CamelSaslXOAuth2YahooPrivate *priv;
};

struct _CamelSaslXOAuth2YahooClass {
	CamelSaslXOAuth2Class parent_class;
};

GType		camel_sasl_xoauth2_yahoo_get_type	(void) G_GNUC_CONST;

G_END_DECLS

#endif /* CAMEL_SASL_XOAUTH2_YAHOO_H */

/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef CAMEL_SPOOL_SETTINGS_H
#define CAMEL_SPOOL_SETTINGS_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SPOOL_SETTINGS \
	(camel_spool_settings_get_type ())
#define CAMEL_SPOOL_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SPOOL_SETTINGS, CamelSpoolSettings))
#define CAMEL_SPOOL_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SPOOL_SETTINGS, CamelSpoolSettingsClass))
#define CAMEL_IS_SPOOL_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SPOOL_SETTINGS))
#define CAMEL_IS_SPOOL_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SPOOL_SETTINGS))
#define CAMEL_SPOOL_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SPOOL_SETTINGS, CamelSpoolSettingsClass))

G_BEGIN_DECLS

typedef struct _CamelSpoolSettings CamelSpoolSettings;
typedef struct _CamelSpoolSettingsClass CamelSpoolSettingsClass;
typedef struct _CamelSpoolSettingsPrivate CamelSpoolSettingsPrivate;

struct _CamelSpoolSettings {
	CamelLocalSettings parent;
	CamelSpoolSettingsPrivate *priv;
};

struct _CamelSpoolSettingsClass {
	CamelLocalSettingsClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_spool_settings_get_type	(void) G_GNUC_CONST;
gboolean	camel_spool_settings_get_use_xstatus_headers
						(CamelSpoolSettings *settings);
void		camel_spool_settings_set_use_xstatus_headers
						(CamelSpoolSettings *settings,
						 gboolean use_xstatus_headers);
gboolean	camel_spool_settings_get_listen_notifications
						(CamelSpoolSettings *settings);
void		camel_spool_settings_set_listen_notifications
						(CamelSpoolSettings *settings,
						 gboolean listen_notifications);

G_END_DECLS

#endif /* CAMEL_SPOOL_SETTINGS_H */

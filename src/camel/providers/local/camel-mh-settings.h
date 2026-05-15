/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef CAMEL_MH_SETTINGS_H
#define CAMEL_MH_SETTINGS_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MH_SETTINGS \
	(camel_mh_settings_get_type ())
#define CAMEL_MH_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MH_SETTINGS, CamelMhSettings))
#define CAMEL_MH_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MH_SETTINGS, CamelMhSettingsClass))
#define CAMEL_IS_MH_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MH_SETTINGS))
#define CAMEL_IS_MH_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MH_SETTINGS))
#define CAMEL_MH_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MH_SETTINGS, CamelMhSettingsClass))

G_BEGIN_DECLS

typedef struct _CamelMhSettings CamelMhSettings;
typedef struct _CamelMhSettingsClass CamelMhSettingsClass;
typedef struct _CamelMhSettingsPrivate CamelMhSettingsPrivate;

struct _CamelMhSettings {
	CamelLocalSettings parent;
	CamelMhSettingsPrivate *priv;
};

struct _CamelMhSettingsClass {
	CamelLocalSettingsClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mh_settings_get_type	(void) G_GNUC_CONST;
gboolean	camel_mh_settings_get_use_dot_folders
						(CamelMhSettings *settings);
void		camel_mh_settings_set_use_dot_folders
						(CamelMhSettings *settings,
						 gboolean use_dot_folders);

G_END_DECLS

#endif /* CAMEL_MH_SETTINGS_H */

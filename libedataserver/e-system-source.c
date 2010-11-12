/*
 * e-system-source.c
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

#include "e-system-source.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include "e-data-server-util.h"

G_DEFINE_TYPE (
	ESystemSource,
	e_system_source,
	E_TYPE_SOURCE)

static void
system_source_notify (GObject *object,
                      GParamSpec *pspec)
{
	ESource *source = E_SOURCE (object);

	/* GObject does not allow subclasses to override property flags,
	 * so we'll keep the "backend-name" and "display-name" properties
	 * fixed by intercepting attempts to change them and setting them
	 * back to their proper values.  Hokey but works. */

	if (g_strcmp0 (pspec->name, "backend-name") == 0) {
		if (e_source_get_backend_name (source) != NULL) {
			e_source_set_backend_name (source, NULL);
			return;
		}
	}

	if (g_strcmp0 (pspec->name, "display-name") == 0) {
		const gchar *display_name;
		const gchar *proper_value;

		display_name = e_source_get_display_name (source);
		proper_value = _("Personal");

		if (g_strcmp0 (display_name, proper_value) != 0) {
			e_source_set_display_name (source, proper_value);
			return;
		}
	}

	/* Chain up to parent's notify() method. */
	G_OBJECT_CLASS (e_system_source_parent_class)->notify (object, pspec);
}

static void
e_system_source_class_init (ESystemSourceClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->notify = system_source_notify;
}

static void
e_system_source_init (ESystemSource *source)
{
}

ESource *
e_system_source_new (void)
{
	GSettings *settings;
	ESource *source;
	GFile *file;
	const gchar *data_dir;
	gchar *path;

	data_dir = e_get_user_data_dir ();
	path = g_build_filename (data_dir, "sources", "system", NULL);
	file = g_file_new_for_path (path);
	g_free (path);

	/* This function must not fail, so if a "system" key file
	 * exists and fails to load, delete it and try again. */
	source = g_initable_new (
		E_TYPE_SYSTEM_SOURCE, NULL, NULL, "file", file, NULL);
	if (source == NULL) {
		g_file_delete (file, NULL, NULL);
		source = g_initable_new (
			E_TYPE_SYSTEM_SOURCE, NULL, NULL, "file", file, NULL);
	}

	g_object_unref (file);

	g_return_val_if_fail (E_IS_SYSTEM_SOURCE (source), NULL);

	/* Set the "parent" key directly through its GSettings.
	 *
	 * XXX To set this during object construction we would have
	 *     to override the GInitable interface.  Too much hassle
	 *     for now.  Maybe revisit this in the future. */
	settings = e_source_get_settings (source);
	g_settings_set_string (settings, "parent", "local");

	return source;
}

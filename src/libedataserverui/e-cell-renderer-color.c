/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-cell-renderer-color.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <gtk/gtk.h>

#include "e-cell-renderer-color.h"

enum {
	PROP_0,
	PROP_RGBA
};

struct _ECellRendererColorPrivate {
	GdkRGBA rgba;
};

G_DEFINE_TYPE_WITH_PRIVATE (
	ECellRendererColor,
	e_cell_renderer_color,
	GTK_TYPE_CELL_RENDERER)

static void
#if GTK_CHECK_VERSION(4, 0, 0)
cell_renderer_color_get_aligned_area (GtkCellRenderer *cell,
				      GtkWidget *widget,
				      GtkCellRendererState flags,
				      const GdkRectangle *cell_area,
				      GdkRectangle *aligned_area)
#else
cell_renderer_color_get_size (GtkCellRenderer *cell,
                              GtkWidget *widget,
                              const GdkRectangle *cell_area,
                              gint *x_offset,
                              gint *y_offset,
                              gint *width,
                              gint *height)
#endif
{
	gint color_width = 16;
	gint color_height = 16;
	gint calc_width;
	gint calc_height;
	gint xx, yy;
	gfloat xalign;
	gfloat yalign;
	guint xpad;
	guint ypad;

#if GTK_CHECK_VERSION(4, 0, 0)
	if (!aligned_area)
		return;
#endif

	g_object_get (
		cell, "xalign", &xalign, "yalign", &yalign,
		"xpad", &xpad, "ypad", &ypad, NULL);

	calc_width = (gint) xpad * 2 + color_width;
	calc_height = (gint) ypad * 2 + color_height;

	if (cell_area && color_width > 0 && color_height > 0) {
		xx = (((gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL) ?
				(1.0 - xalign) : xalign) *
				(cell_area->width - calc_width));
		xx = MAX (xx, 0);

		yy = yalign * (cell_area->height - calc_height);
		yy = MAX (yy, 0);
	} else {
		xx = 0;
		yy = 0;
	}

#if GTK_CHECK_VERSION(4, 0, 0)
	aligned_area->x = xx;
	aligned_area->y = yy;
	aligned_area->width = calc_width;
	aligned_area->height = calc_height;
#else
	if (x_offset)
		*x_offset = xx;
	if (y_offset)
		*y_offset = yy;
	if (width)
		*width = calc_width;
	if (height)
		*height = calc_height;
#endif
}

static void
#if GTK_CHECK_VERSION(4, 0, 0)
cell_renderer_color_snapshot (GtkCellRenderer *cell,
			      GtkSnapshot *snapshot,
			      GtkWidget *widget,
			      const GdkRectangle *background_area,
			      const GdkRectangle *cell_area,
			      GtkCellRendererState flags)
#else
cell_renderer_color_render (GtkCellRenderer *cell,
                            cairo_t *cr,
                            GtkWidget *widget,
                            const GdkRectangle *background_area,
                            const GdkRectangle *cell_area,
                            GtkCellRendererState flags)
#endif
{
#if GTK_CHECK_VERSION(4, 0, 0)
	graphene_rect_t rect;
#endif
	ECellRendererColorPrivate *priv;
	GdkRectangle pix_rect;
	GdkRectangle draw_rect;
	guint xpad;
	guint ypad;

	priv = E_CELL_RENDERER_COLOR (cell)->priv;

#if GTK_CHECK_VERSION(4, 0, 0)
	cell_renderer_color_get_aligned_area (cell, widget, 0, cell_area, &pix_rect);
#else
	cell_renderer_color_get_size (
		cell, widget, cell_area,
		&pix_rect.x, &pix_rect.y,
		&pix_rect.width, &pix_rect.height);
#endif

	g_object_get (cell, "xpad", &xpad, "ypad", &ypad, NULL);

	pix_rect.x += cell_area->x + xpad;
	pix_rect.y += cell_area->y + ypad;
	pix_rect.width  -= xpad * 2;
	pix_rect.height -= ypad * 2;

	if (!gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect))
		return;

#if GTK_CHECK_VERSION(4, 0, 0)
	rect.origin.x = pix_rect.x;
	rect.origin.y = pix_rect.y;
	rect.size.width = draw_rect.width;
	rect.size.height = draw_rect.height;

	gtk_snapshot_append_color (snapshot, &priv->rgba, &rect);
#else
	gdk_cairo_set_source_rgba (cr, &priv->rgba);
	cairo_rectangle (cr, pix_rect.x, pix_rect.y, draw_rect.width, draw_rect.height);

	cairo_fill (cr);
#endif
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void
cell_renderer_color_get_preferred_width (GtkCellRenderer *cell,
					 GtkWidget *widget,
					 gint *minimum_size,
					 gint *natural_size)
{
	gint xpad = 0, ypad = 0;

	gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

	if (minimum_size)
		*minimum_size = 16 + xpad;
	if (natural_size)
		*natural_size = 16 + xpad;
}

static void
cell_renderer_color_get_preferred_height (GtkCellRenderer *cell,
					  GtkWidget *widget,
					  gint *minimum_size,
					  gint *natural_size)
{
	gint xpad = 0, ypad = 0;

	gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

	if (minimum_size)
		*minimum_size = 16 + ypad;
	if (natural_size)
		*natural_size = 16 + ypad;
}
#endif

static void
cell_renderer_color_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
	ECellRendererColorPrivate *priv;
	GdkRGBA *rgba;

	priv = E_CELL_RENDERER_COLOR (object)->priv;

	switch (property_id) {
		case PROP_RGBA:
			rgba = g_value_dup_boxed (value);
			if (rgba) {
				priv->rgba = *rgba;
				gdk_rgba_free (rgba);
			} else {
				priv->rgba.red = 0.0;
				priv->rgba.green = 0.0;
				priv->rgba.blue = 0.0;
				priv->rgba.alpha = 0.0;
			}
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cell_renderer_color_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
	ECellRendererColorPrivate *priv;

	priv = E_CELL_RENDERER_COLOR (object)->priv;

	switch (property_id) {
		case PROP_RGBA:
			g_value_set_boxed (value, &priv->rgba);
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_cell_renderer_color_class_init (ECellRendererColorClass *class)
{
	GObjectClass *object_class;
	GtkCellRendererClass *cell_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cell_renderer_color_set_property;
	object_class->get_property = cell_renderer_color_get_property;

	cell_class = GTK_CELL_RENDERER_CLASS (class);
#if GTK_CHECK_VERSION(4, 0, 0)
	cell_class->get_aligned_area = cell_renderer_color_get_aligned_area;
	cell_class->snapshot = cell_renderer_color_snapshot;
	cell_class->get_preferred_width = cell_renderer_color_get_preferred_width;
	cell_class->get_preferred_height = cell_renderer_color_get_preferred_height;
#else
	cell_class->get_size = cell_renderer_color_get_size;
	cell_class->render = cell_renderer_color_render;
#endif

	g_object_class_install_property (
		object_class,
		PROP_RGBA,
		g_param_spec_boxed (
			"rgba",
			"Color Info",
			"The GdkRGBA color to render",
			GDK_TYPE_RGBA,
			G_PARAM_READWRITE));
}

static void
e_cell_renderer_color_init (ECellRendererColor *cellcolor)
{
	cellcolor->priv = e_cell_renderer_color_get_instance_private (cellcolor);

	g_object_set (cellcolor, "xpad", 4, NULL);
}

/**
 * e_cell_renderer_color_new:
 *
 * Since: 2.22
 **/
GtkCellRenderer *
e_cell_renderer_color_new (void)
{
	return g_object_new (E_TYPE_CELL_RENDERER_COLOR, NULL);
}

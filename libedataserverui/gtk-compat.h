#ifndef __GTK_COMPAT_H__
#define __GTK_COMPAT_H__

#include <gtk/gtk.h>

/* Provide a compatibility layer for accessor functions introduced
 * in GTK+ 2.21.1 which we need to build with sealed GDK.
 * That way it is still possible to build with GTK+ 2.20.
 */

#if (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 21) \
	|| (GTK_MINOR_VERSION == 21 && GTK_MICRO_VERSION < 1)

#define gdk_drag_context_get_actions(context)          (context)->actions
#define gdk_drag_context_get_suggested_action(context) (context)->suggested_action
#define gdk_drag_context_get_selected_action(context)  (context)->action

#endif

#if GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION == 21 && GTK_MICRO_VERSION == 1
#define gdk_drag_context_get_selected_action(context)  gdk_drag_context_get_action(context)
#endif

#if !GTK_CHECK_VERSION (2,23,0)
#define gtk_combo_box_text_new			gtk_combo_box_new_text
#define gtk_combo_box_text_append_text		gtk_combo_box_append_text
#define gtk_combo_box_text_get_active_text	gtk_combo_box_get_active_text
#endif

#if GTK_CHECK_VERSION (2,90,5)

/* Recreate GdkRegion until we drop GTK2 compatibility. */

#define GdkRegion cairo_region_t

#define gdk_region_destroy(region) \
	(cairo_region_destroy (region))

#define gdk_region_point_in(region, x, y) \
	(cairo_region_contains_point ((region), (x), (y)))

#define gdk_region_rectangle(rectangle) \
	(((rectangle)->width <= 0 || (rectangle->height <= 0)) ? \
	cairo_region_create () : cairo_region_create_rectangle (rectangle))

#endif

#endif /* __GTK_COMPAT_H__ */

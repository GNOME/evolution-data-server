#ifndef __GTK_COMPAT_H__
#define __GTK_COMPAT_H__

#include <gtk/gtk.h>

/* Provide a GTK+ compatibility layer. */

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

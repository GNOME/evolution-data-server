#ifndef __GTK_COMPAT_H__
#define __GTK_COMPAT_H__

#include <gtk/gtk.h>

/* Provide a GTK+ compatibility layer. */

#if !GTK_CHECK_VERSION (2,23,0)
#define gtk_combo_box_text_new			gtk_combo_box_new_text
#define gtk_combo_box_text_append_text		gtk_combo_box_append_text
#define gtk_combo_box_text_get_active_text	gtk_combo_box_get_active_text
#define GTK_COMBO_BOX_TEXT			GTK_COMBO_BOX

/* The below can be used only once in sources */
#define ENSURE_GTK_COMBO_BOX_TEXT_TYPE						\
	GType gtk_combo_box_text_get_type (void);				\
	typedef struct _GtkComboBoxText GtkComboBoxText;			\
	typedef struct _GtkComboBoxTextClass GtkComboBoxTextClass;		\
										\
	struct _GtkComboBoxText {						\
		GtkComboBox parent;						\
	};									\
										\
	struct _GtkComboBoxTextClass {						\
		GtkComboBoxClass parent_class;					\
	};									\
										\
										\
	G_DEFINE_TYPE (GtkComboBoxText, gtk_combo_box_text, GTK_TYPE_COMBO_BOX)	\
										\
	static void gtk_combo_box_text_init (GtkComboBoxText *cbt) {}		\
	static void gtk_combo_box_text_class_init (GtkComboBoxTextClass *kl) {}

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

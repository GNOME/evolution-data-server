#ifndef __E_NAME_WESTERN_H__
#define __E_NAME_WESTERN_H__

#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

typedef struct {

	/* Public */
	gchar *prefix;
	gchar *first;
	gchar *middle;
	gchar *nick;
	gchar *last;
	gchar *suffix;

	/* Private */
	gchar *full;
} ENameWestern;

ENameWestern *e_name_western_parse (const gchar   *full_name);
void          e_name_western_free  (ENameWestern *w);

G_END_DECLS

#endif /* __E_NAME_WESTERN_H__ */

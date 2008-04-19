#ifndef __E_NAME_WESTERN_H__
#define __E_NAME_WESTERN_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct {

	/* Public */
	char *prefix;
	char *first;
	char *middle;
	char *nick;
	char *last;
	char *suffix;

	/* Private */
	char *full;
} ENameWestern;

ENameWestern *e_name_western_parse (const char   *full_name);
void          e_name_western_free  (ENameWestern *w);

G_END_DECLS

#endif /* ! __E_NAME_WESTERN_H__ */

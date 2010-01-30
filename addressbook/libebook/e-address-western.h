#ifndef __E_ADDRESS_WESTERN_H__
#define __E_ADDRESS_WESTERN_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct {

	/* Public */
	gchar *po_box;
	gchar *extended;  /* I'm not sure what this is. */
	gchar *street;
	gchar *locality;  /* For example, the city or town. */
	gchar *region;	/* The state or province. */
	gchar *postal_code;
	gchar *country;
} EAddressWestern;

EAddressWestern *e_address_western_parse (const gchar *in_address);
void e_address_western_free (EAddressWestern *eaw);

G_END_DECLS

#endif  /* !__E_ADDRESS_WESTERN_H__ */


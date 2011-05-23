#include <stdlib.h>
#include <string.h>
#include <libebook/e-contact.h>

static const gchar *photo_data =
"/9j/4AAQSkZJRgABAQEARwBHAAD//gAXQ3JlYXRlZCB3aXRoIFRoZSBHSU1Q/9sAQwAIBgYHB\
gUIBwcHCQkICgwUDQwLCwwZEhMPFB0aHx4dGhwcICQuJyAiLCMcHCg3KSwwMTQ0NB8nOT04Mjw\
uMzQy/9sAQwEJCQkMCwwYDQ0YMiEcITIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyM\
jIyMjIyMjIyMjIyMjIyMjIy/8AAEQgAMgAyAwEiAAIRAQMRAf/EABsAAQACAwEBAAAAAAAAAAA\
AAAAHCAQFBgID/8QAMBAAAgEDAQYEBQQDAAAAAAAAAQIDAAQRBQYSEyExQQdhcYEiI0JRkRQVM\
qFiguH/xAAaAQADAQEBAQAAAAAAAAAAAAAABAUCBgED/8QAIxEAAgICAQQCAwAAAAAAAAAAAAE\
CAwQRQRITITEUYQUiUf/aAAwDAQACEQMRAD8An+sHUtWtNKjVrmQ7754cajLvjrgfbzPIdzWdV\
fds9pJb3XdQkMrcFZGj+HqY0bdVV9Tz/wBia+N9vbjvkaxMb5E9N6SJB1HxLEEjJaWsUjD6QzS\
MPXdGB7E1zV74t63HINy1s4F7CWCTn77wrA0TY86jY3N1qsUk6wxBxBDvYjLHkoUH4j3JP/a0V\
3s1CvF/QM9tKpw0THeU+TLkj8VLnmzT8y0n9FujBx5bioba/rZLWx3iPZ7RzLp95GtnqRGVTez\
HNjruH7/4n+67iqpq7Qi3uYWMMsNynfnE6sM8/Lr6VamFi0KMepUE1Sx7XZHbI+fjxos1H0z3S\
lKYEjzISI2I64OKqsyu8sck2QYrmPjBvpIYg598Vauoh8VtlY7JW2isoBwpPl6hGByZTyD+o6E\
+h7UtlVOcPHA/+PyI1Wal6Zp7vaC/06wnTTLtEeUDiKwzu4H8vI9AM9Tiuctkng1Nnk1G5cOoY\
ifB4nI/jB7VjWuoT21qPmwXUCHKlphHKvqG5N6g0/cLi/Rg88FhbkbxlaUSu3kqpnn6kDzqGqb\
NdPB0XyK4/svZr9RVntL50GePdcKEDqzhVBx7sKtPpayppNosxzKIlDHzxUFeG2zo2n2kivWhK\
6PpHwwoTnfk65J7kZyT9z5VYADAwKuYtfRA5zPv7tnjgUpSmREV8bq1hvbWW1uY1khlUo6MMhg\
eor7UoAje18FtmLe9eeQT3EXPcglkJRPbv71EWu7Dajp2o3MGmlRCkjKQ30jPUe1WlrlNW0Rpt\
TleNB84DnjkD0P9VlxT4Nqck9pmn8JuFp2zo0cgCWFi2e7555/NSHXLadso2m3sU0NxlV65HM+\
VdTW3rgwvsUpSvAFKUoAUxSlAClKUAKUpQB//2Q==";

gint
main (gint argc, gchar **argv)
{
	EContact *contact;
	EContactPhoto *photo, *new_photo;
	guchar *data;
	gsize length = 0;

	g_type_init ();

	contact = e_contact_new ();
	data = g_base64_decode (photo_data, &length);

	photo = g_new (EContactPhoto, 1);
	photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
	photo->data.inlined.mime_type = NULL;
	photo->data.inlined.data = data;
	photo->data.inlined.length = length;

	/* set the photo */
	e_contact_set (contact, E_CONTACT_PHOTO, photo);

	/* then get the photo */
	new_photo = e_contact_get (contact, E_CONTACT_PHOTO);

	/* and compare */
	if (new_photo->data.inlined.length != photo->data.inlined.length)
	  g_error ("photo lengths differ");

	if (memcmp (new_photo->data.inlined.data, photo->data.inlined.data, photo->data.inlined.length))
	  g_error ("photo data differs");

	printf ("photo test passed\n");

	return 0;
}

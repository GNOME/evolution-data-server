/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdio.h>
#include <libebook/e-vcard.h>

gint
main(gint argc, gchar **argv)
{
	FILE *fp;
	EVCard *vcard;
	GString *str = g_string_new ("");
	gchar *parsed_vcard;

	if (argc < 2)
	  return 0;

	g_type_init_with_debug_flags (G_TYPE_DEBUG_OBJECTS);

	fp = fopen (argv[1], "r");

	while (!feof (fp)) {
		gchar buf[1024];
		if (fgets (buf, sizeof(buf), fp))
			str = g_string_append (str, buf);
	}
	fclose (fp);

	vcard = e_vcard_new_from_string (str->str);

	e_vcard_dump_structure (vcard);

	parsed_vcard = e_vcard_to_string (vcard, EVC_FORMAT_VCARD_30);

	printf ("\nvcard: %s\n", parsed_vcard);

	g_object_unref (vcard);

	g_free (parsed_vcard);

	return 0;
}

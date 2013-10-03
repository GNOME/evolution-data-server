/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libebook-contacts/libebook-contacts.h>
#include <locale.h>

static gboolean   inspect = FALSE;
static gboolean   compare = FALSE;
static gchar    **numbers = NULL;

static GOptionEntry option_entries[] = {
	{ "inspect", 'i', 0, G_OPTION_ARG_NONE, &inspect, "Inspect the input number", NULL },
	{ "compare", 'c', 0, G_OPTION_ARG_NONE, &compare, "Compare two phone numbers", NULL },
	{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_STRING_ARRAY, &numbers, "The phone numbers", "[PHONE1, ...]" },
	{ NULL }
};

static EPhoneNumber *
parse_phone_number (const gchar  *input_number,
		    GError      **error)
{
	EPhoneNumber *phone_number = NULL;
	gchar **split;

	split = g_strsplit (input_number, ",", 2);

	if (split && split[0] && split[1])
		phone_number = e_phone_number_from_string (split[1],
							   split[0],
							   error);

	return phone_number;
}

#define COUNTRY_SOURCE_STR(source) \
	((source) == E_PHONE_NUMBER_COUNTRY_FROM_FQTN ? "Fully qualified telephone number" : \
	 (source) == E_PHONE_NUMBER_COUNTRY_FROM_IDD ? "Local call prefex followed by country code" : \
	 (source) == E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT ? "Guessed country code from current locale" : "Unknown")

#define MATCH_TO_STR(match)						\
	((match) == E_PHONE_NUMBER_MATCH_NONE     ? "None" :		\
	 (match) == E_PHONE_NUMBER_MATCH_EXACT    ? "Exact" :		\
	 (match) == E_PHONE_NUMBER_MATCH_NATIONAL ? "National" :	\
	 (match) == E_PHONE_NUMBER_MATCH_SHORT    ? "Short" : "Unknown")

gint
main (gint argc, gchar *argv[])
{
	GOptionContext *option_context;
	GOptionGroup *option_group;
	GError *error = NULL;

	option_context = g_option_context_new (NULL);
	g_option_context_set_summary (option_context,
				      "Inspect or compare phone numbers\n\n"
				      "Phone numbers must be specified as strings with a locale name\n"
				      "followed by a phone number separated by a comma.\n\n"
				      "EXAMPLE: \"en_US, +1 555 678 1234\"");

	option_group = g_option_group_new ("Inspect or compare phone numbers",
					   "Inspect or compare phone numbers",
					   "Inspect or compare phone numbers", NULL, NULL);
	g_option_group_add_entries (option_group, option_entries);
	g_option_context_set_main_group (option_context, option_group);

	if (!g_option_context_parse (option_context, &argc, &argv, &error))
		g_error ("Failed to parse program arguments: %s", error->message);

	if (!compare && !inspect)
		g_error ("Must specify one of the -c or -i options\n%s",
			 g_option_context_get_help (option_context, TRUE, NULL));

	if (!numbers || !numbers[0] || (compare && !numbers[1]))
		g_error ("Not enough phone numbers specified for the '%s' option\n%s",
			 compare ? "-c" : "-i",
			 g_option_context_get_help (option_context, TRUE, NULL));

	if (inspect) {
		EPhoneNumber *number = parse_phone_number (numbers[0], &error);
		EPhoneNumberCountrySource country_source = E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT;

		if (!number) {
			g_print ("Invalid phone number, failed with: %s\n", error->message);
			g_clear_error (&error);
		} else {
			gint   country_code    = e_phone_number_get_country_code (number, &country_source);
			gchar *national_number = e_phone_number_get_national_number (number);
			gchar *e164_number     = e_phone_number_to_string (number, E_PHONE_NUMBER_FORMAT_E164);

			g_print ("\tCountry Code: %d (Source: %s)\n", country_code, COUNTRY_SOURCE_STR(country_source));
			g_print ("\tNational Number: %s\n", national_number);
			g_print ("\tE.164 Number:%s\n", e164_number);

			g_free (national_number);
			g_free (e164_number);

			e_phone_number_free (number);
		}

	} else {
		EPhoneNumberMatch match;
		EPhoneNumber *number_a, *number_b;

		number_a = parse_phone_number (numbers[0], &error);
		if (number_a) {

			number_b = parse_phone_number (numbers[1], &error);
			if (number_b) {

				match = e_phone_number_compare (number_a, number_b);
				g_print ("\tNumbers match with strength: %s\n", MATCH_TO_STR (match));

				e_phone_number_free (number_b);

			} else {
				g_print ("Invalid phone number '%s': %s\n",
					 numbers[1], error->message);
			}

			e_phone_number_free (number_a);

		} else {
			g_print ("Invalid phone number '%s': %s\n",
				 numbers[0], error->message);
		}
	}

	return 0;
}

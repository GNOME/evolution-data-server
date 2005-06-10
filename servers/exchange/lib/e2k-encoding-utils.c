/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e2k-encoding-utils.h"

#include <string.h>

static const char *b64_alphabet =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * e2k_base64_encode:
 * @data: binary data
 * @len: the length of @data
 *
 * Base64-encodes @len bytes of data at @data.
 *
 * Return value: the base64-encoded representation of @data, which
 * the caller must free.
 **/
char *
e2k_base64_encode (const guint8 *data, int len)
{
	char *buf, *p;

	p = buf = g_malloc (((len + 2) / 3) * 4 + 1);
	while (len >= 3) {
		p[0] = b64_alphabet[ (data[0] >> 2) & 0x3f];
		p[1] = b64_alphabet[((data[0] << 4) & 0x30) +
				    ((data[1] >> 4) & 0x0f)];
		p[2] = b64_alphabet[((data[1] << 2) & 0x3c) +
				    ((data[2] >> 6) & 0x03)];
		p[3] = b64_alphabet[  data[2]       & 0x3f];

		data += 3;
		p += 4;
		len -= 3;
	}

	switch (len) {
	case 2:
		p[0] = b64_alphabet[ (data[0] >> 2) & 0x3f];
		p[1] = b64_alphabet[((data[0] << 4) & 0x30) +
				    ((data[1] >> 4) & 0xf)];
		p[2] = b64_alphabet[ (data[1] << 2) & 0x3c];
		p[3] = '=';
		p += 4;
		break;
	case 1:
		p[0] = b64_alphabet[ (data[0] >> 2) & 0x3f];
		p[1] = b64_alphabet[ (data[0] << 4) & 0x30];
		p[2] = '=';
		p[3] = '=';
		p += 4;
		break;
	}

	*p = '\0';
	return buf;
}

static guint8 base64_unphabet[] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
	-1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
};
#define E2K_B64_SPACE ((guint8)-2)
#define E2K_B64_BAD   ((guint8)-1)

/**
 * e2k_base64_decode:
 * @string: base64-encoded data
 *
 * Decodes the base64-encoded data in @string.
 *
 * Return value: the binary data encoded in @string
 **/
GByteArray *
e2k_base64_decode (const char *string)
{
	GByteArray *rc;
	int bits, length, qw = 0;
	guint8 *data;

	rc = g_byte_array_new ();

	length = strlen (string);
	if (length == 0)
		return rc;

	g_byte_array_set_size (rc, ((length / 4) + 1) * 3);

	data = rc->data;
	for (; *string; string++) {
		if ((unsigned char)*string > 127)
			break;
		bits = base64_unphabet[(unsigned char)*string];
		if (bits == E2K_B64_BAD)
			break;
		else if (bits == E2K_B64_SPACE)
			continue;

		switch (qw++) {
		case 0:
			data[0]  = (bits << 2) & 0xfc;
			break;
		case 1:
			data[0] |= (bits >> 4) & 0x03;
			data[1]  = (bits << 4) & 0xf0;
			break;
		case 2:
			data[1] |= (bits >> 2) & 0x0f;
			data[2]  = (bits << 6) & 0xc0;
			break;
		case 3:
			data[2] |=  bits       & 0x3f;
			data += 3;
			qw = 0;
			break;
		}
	}

	rc->len = data - rc->data;
	if (qw > 1)
		rc->len += qw - 1;
	return rc;
}

/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* camel-openchange-transport.c: Openchange transport class. */

#include "oc.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <camel/camel-data-wrapper.h>
#include <camel/camel-exception.h>
#include <camel/camel-mime-filter-crlf.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>
#include <camel/camel-session.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-stream-mem.h>


#include "camel-mapi-transport.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <camel/camel-sasl.h>
#include <camel/camel-utf8.h>
#include <camel/camel-tcp-stream-raw.h>

#ifdef HAVE_SSL
#include <camel/camel-tcp-stream-ssl.h>
#endif


#include <camel/camel-private.h>
#include <camel/camel-i18n.h>
#include <camel/camel-net-utils.h>
#include "camel-mapi-store.h"
#include "camel-mapi-folder.h"
#include "camel-mapi-store-summary.h"
#include <camel/camel-session.h>
#include <camel/camel-store-summary.h>
#define d(x) x

#include <camel/camel-seekable-stream.h>
CamelStore *get_store(void);

void	set_store(CamelStore *);
gboolean mapi_initialize();

/*
** this function is used to send message
*/
static gboolean do_multipart(CamelMultipart *mp,
			     oc_message_headers_t *headers,
			     oc_message_contents_t *contents)
{
	CamelDataWrapper	*dw;
	CamelStream		*stream;
	CamelContentType	*type;
	CamelMimePart		*part;
	int			n_part;
	int			i_part;
	const char		*filename;
	const char		*description;
	const char		*content_id;
	int			sz_content;

	n_part = camel_multipart_get_number(mp);
	for (i_part = 0; i_part < n_part; i_part++) {
		/* getting part */
		part = camel_multipart_get_part(mp, i_part);
		dw = camel_medium_get_content_object (CAMEL_MEDIUM (part));
		if (CAMEL_IS_MULTIPART(dw)) {
			/* recursive */
			if (!do_multipart(CAMEL_MULTIPART(dw), headers, contents))
				return FALSE;
			continue ;
		}

    		/* getting part information */
	
		/* filename */
		filename = camel_mime_part_get_filename(part);
		/* getting body */
		dw = camel_medium_get_content_object(CAMEL_MEDIUM(part));
		stream = camel_stream_mem_new();
		sz_content = camel_data_wrapper_decode_to_stream (dw, (CamelStream *) stream);
		camel_seekable_stream_seek((CamelSeekableStream *)stream, 0, CAMEL_STREAM_SET);
		/* description */
		description = camel_mime_part_get_description(part);
		content_id = camel_mime_part_get_content_id(part);
		
		/* openchange setting */
		type = camel_mime_part_get_content_type(part);
		if (i_part == 0 && camel_content_type_is (type, "text", "plain")) {
			oc_message_contents_set_body(contents, stream);
		} else {
			oc_message_contents_add_attach(contents, filename,
						       description, stream,
						       (int)sz_content);
		}
	}

  return TRUE;
}


static gboolean openchange_send_to (CamelTransport *transport,
				    CamelMimeMessage *message,
				    CamelAddress *from,
				    CamelAddress *recipients,
				    CamelException *ex)
{
	CamelDataWrapper		*dw;
	CamelContentType		*type;
	CamelStream			*content_stream;
	const CamelInternetAddress   	*to, *cc, *bcc;
	oc_message_contents_t		contents;
	oc_message_headers_t		headers;
	const char			*namep;
	const char			*addressp;
	const char			*content_type;		
	int				i;
	int				st;
	ssize_t				sz;
	


	/* headers */
	oc_message_headers_init(&headers);
	if (!camel_internet_address_get((const CamelInternetAddress *)from, 0, &namep, &addressp)) {
		printf("index\n");
		return (FALSE);
	}
	/** WARNING: double check **/
	oc_message_headers_set_from(&headers, namep);

	oc_thread_connect_lock();
	mapi_initialize();
	
	to = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_TO);
	for (i = 0; camel_internet_address_get(to, i, &namep, &addressp); i++){
		oc_message_headers_add_recipient(&headers, addressp);
	}

	cc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_CC);
	for (i = 0; camel_internet_address_get(cc, i, &namep, &addressp); i++) {
		oc_message_headers_add_recipient_cc(&headers, addressp);
	}

	bcc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_BCC);
	for (i = 0; camel_internet_address_get(bcc, i, &namep, &addressp); i++) {
		oc_message_headers_add_recipient_bcc(&headers, addressp);
	}
	
	if (camel_mime_message_get_subject(message)) {
		oc_message_headers_set_subject(&headers, camel_mime_message_get_subject(message));
	}

	/* contents body */
	dw = camel_medium_get_content_object (CAMEL_MEDIUM (message));
	oc_message_contents_init(&contents);
	if (CAMEL_IS_MULTIPART(dw)) {
		if (do_multipart(CAMEL_MULTIPART(dw), &headers, &contents) == FALSE) {
			printf("camel message multi part error\n");
		}
	} else {
		content_stream = (CamelStream *)camel_stream_mem_new();
		type = camel_mime_part_get_content_type((CamelMimePart *)message);
		content_type = camel_content_type_simple (type);
		sz = camel_data_wrapper_write_to_stream(dw, (CamelStream *)content_stream);
		oc_message_contents_set_body(&contents, content_stream);
	}
	
	
	/* send */
	st = oc_message_send(&headers, &contents);
	oc_thread_connect_unlock();
	if (st == -1) {
		printf("[!] cannot send(%s)\n", headers.to);
		return (FALSE);
	}
	
	return (TRUE);
}


static char *openchange_trans_get_name(CamelService *service, gboolean brief)
{
	if (brief) {
		return g_strdup_printf (_("Openchange server %s"), service->url->host);
	} else {
		return g_strdup_printf (_("openchange service for %s on %s"),
					service->url->user, service->url->host);
	}
}


static void camel_openchange_transport_class_init(CamelOpenchangeTransportClass *camel_openchange_transport_class)
{
	CamelTransportClass *camel_transport_class =
		CAMEL_TRANSPORT_CLASS (camel_openchange_transport_class);
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_openchange_transport_class);
  
	camel_service_class->get_name = openchange_trans_get_name;
	camel_transport_class->send_to = openchange_send_to;
}

static void	camel_openchange_transport_init (CamelTransport *transport)
{

}

CamelType	camel_openchange_transport_get_type (void)
{
	static CamelType camel_openchange_transport_type = CAMEL_INVALID_TYPE;
  
	if (camel_openchange_transport_type == CAMEL_INVALID_TYPE) {
		camel_openchange_transport_type =
			camel_type_register (CAMEL_TRANSPORT_TYPE,
					     "CamelOpenchangeTransport",
					     sizeof (CamelOpenchangeTransport),
					     sizeof (CamelOpenchangeTransportClass),
					     (CamelObjectClassInitFunc) camel_openchange_transport_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_openchange_transport_init,
					     NULL);
	}

	return camel_openchange_transport_type;
}


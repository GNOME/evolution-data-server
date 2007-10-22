/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *  Copyright (C) Fabien Le-Mentec 2007.
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



#ifndef	__OC_H__
#define	__OC_H__


#define __USE_GNU /* for comparison_fn_t */
#include <libmapi/libmapi.h>
#include <string.h>
#include <param.h>
#include <glib.h>

#include <camel/camel-stream.h>

/**
 * DEFINES
 */

#define PATH_LDB	".evolution/mapi-profiles.ldb"
#define	STREAM_SIZE	0x4000

/**
 * DATA STRUCTURES
 */

typedef struct {
	char		*subject;
	char		*from;
	char		*to;
	char		*cc;
	char		*bcc;
	int		flags;
	time_t		send;
} oc_message_headers_t;

typedef struct {
	int		id;
	char		*filename;
	char		*description;
	int		sz_content;
	CamelStream	*buf_content;
} oc_message_attach_t;

typedef struct {
	CamelStream	*body;
	int		n_attach;
	GList		*l_attach;
} oc_message_contents_t;

extern bool		gl_init;


/**
 * PROTOTYPES
 */

#ifndef __BEGIN_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS		extern "C" {
#define __END_DECLS		}
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#endif

__BEGIN_DECLS
/* global */
int	oc_initialize(const char *, const char *);
int	oc_uninitialize(void);
/* folders */
char *folder_mapi_ids_to_uid(mapi_id_t id_folder);
int folder_uid_to_mapi_ids(const char *s, mapi_id_t *id_folder);
int	oc_inbox_list_message_ids(char ***, int *, oc_message_headers_t ***, char *);
void	oc_message_headers_init(oc_message_headers_t *);
int	oc_message_headers_get_by_id(oc_message_headers_t *, const char *);
void	oc_message_headers_release(oc_message_headers_t *);
void	oc_message_headers_set_from(oc_message_headers_t *, const char *);
void	oc_message_headers_set_subject(oc_message_headers_t *, const char *);
void	oc_message_headers_add_recipient(oc_message_headers_t *, const char *);
void	oc_message_headers_add_recipient_cc(oc_message_headers_t *, const char *);
void	oc_message_headers_add_recipient_bcc(oc_message_headers_t *, const char *);
void	oc_message_contents_init(oc_message_contents_t *);
int	oc_message_contents_get_by_id(oc_message_contents_t *, const char *);
int	oc_message_update_flags_by_id(char *, int);
int	oc_message_update_flags_by_n_id(int, char **, int *);
void	oc_message_contents_release(oc_message_contents_t *);
void	oc_message_contents_set_body(oc_message_contents_t *, CamelStream *);
int	oc_message_contents_get_attach(oc_message_contents_t *, int, const oc_message_attach_t **);
int	oc_message_contents_add_attach(oc_message_contents_t *, const char *, const char *, CamelStream *, int);
int	oc_message_send(oc_message_headers_t *, oc_message_contents_t *);
/* deleted mail */
int	oc_delete_mail_by_uid(char *);
__END_DECLS

#endif /* ! __OC_H__ */

/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_CONTEXT_H__
#define __E2K_CONTEXT_H__

#include <libsoup/soup-message.h>
#include <sys/time.h>

#include <glib-object.h>

#include "e2k-types.h"
#include "e2k-operation.h"
#include "e2k-http-utils.h"
#include "e2k-result.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E2K_TYPE_CONTEXT            (e2k_context_get_type ())
#define E2K_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_TYPE_CONTEXT, E2kContext))
#define E2K_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_TYPE_CONTEXT, E2kContextClass))
#define E2K_IS_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_TYPE_CONTEXT))
#define E2K_IS_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E2K_TYPE_CONTEXT))

struct _E2kContext {
	GObject parent;

	E2kContextPrivate *priv;
};

struct _E2kContextClass {
	GObjectClass parent_class;

	/* signals */
	void (*redirect) (E2kContext *ctx, E2kHTTPStatus status,
			  const char *old_uri, const char *new_uri);
};

GType          e2k_context_get_type         (void);

E2kContext    *e2k_context_new              (const char  *uri);
void           e2k_context_set_auth         (E2kContext  *ctx,
					     const char  *username,
					     const char  *domain,
					     const char  *authmech,
					     const char  *password);
gboolean       e2k_context_fba              (E2kContext  *ctx,
					     SoupMessage *failed_msg);

time_t        e2k_context_get_last_timestamp    (E2kContext *ctx);

typedef gboolean (*E2kContextTestCallback)     (E2kContext *ctx,
						const char *test_name,
						gpointer user_data);

E2kHTTPStatus  e2k_context_get               (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      char **content_type,
					      char **body, int *len);
E2kHTTPStatus  e2k_context_get_owa           (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      gboolean claim_ie,
					      char **body, int *len);

E2kHTTPStatus  e2k_context_put               (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      const char *content_type,
					      const char *body, int length,
					      char **repl_uid);
E2kHTTPStatus  e2k_context_put_new           (E2kContext *ctx,
					      E2kOperation *op,
					      const char *folder_uri,
					      const char *object_name,
					      E2kContextTestCallback test_callback,
					      gpointer user_data,
					      const char *content_type,
					      const char *body, int length,
					      char **location,
					      char **repl_uid);
E2kHTTPStatus  e2k_context_post              (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      const char *content_type,
					      const char *body, int length,
					      char **location,
					      char **repl_uid);

E2kHTTPStatus  e2k_context_proppatch         (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      E2kProperties *props,
					      gboolean create,
					      char **repl_uid);
E2kHTTPStatus  e2k_context_proppatch_new     (E2kContext *ctx,
					      E2kOperation *op,
					      const char *folder_uri,
					      const char *object_name,
					      E2kContextTestCallback test_callback,
					      gpointer user_data,
					      E2kProperties *props,
					      char **location,
					      char **repl_uid);
E2kResultIter *e2k_context_bproppatch_start  (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs,
					      E2kProperties *props,
					      gboolean create);

E2kHTTPStatus  e2k_context_propfind          (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      const char **props,
					      int nprops,
					      E2kResult **results,
					      int *nresults);
E2kResultIter *e2k_context_bpropfind_start   (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs,
					      const char **props,
					      int nprops);

E2kResultIter *e2k_context_search_start      (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      const char **props,
					      int nprops,
					      E2kRestriction *rn,
					      const char *orderby,
					      gboolean ascending);

E2kHTTPStatus  e2k_context_delete            (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri);

E2kResultIter *e2k_context_bdelete_start     (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs);

E2kHTTPStatus  e2k_context_mkcol             (E2kContext *ctx,
					      E2kOperation *op,
					      const char *uri,
					      E2kProperties *props,
					      char **permanent_url);

E2kResultIter *e2k_context_transfer_start    (E2kContext *ctx,
					      E2kOperation *op,
					      const char *source_folder,
					      const char *dest_folder,
					      GPtrArray *source_hrefs,
					      gboolean delete_originals);
E2kHTTPStatus  e2k_context_transfer_dir      (E2kContext *ctx,
					      E2kOperation *op,
					      const char *source_href,
					      const char *dest_href,
					      gboolean delete_original,
					      char **permanent_url);

/* Subscriptions */
typedef enum {
	E2K_CONTEXT_OBJECT_CHANGED,
	E2K_CONTEXT_OBJECT_ADDED,
	E2K_CONTEXT_OBJECT_REMOVED,
	E2K_CONTEXT_OBJECT_MOVED
} E2kContextChangeType;

typedef void (*E2kContextChangeCallback)     (E2kContext *ctx,
					      const char *uri,
					      E2kContextChangeType type,
					      gpointer user_data);

void          e2k_context_subscribe          (E2kContext *ctx,
					      const char *uri,
					      E2kContextChangeType type,
					      int min_interval,
					      E2kContextChangeCallback callback,
					      gpointer user_data);
void          e2k_context_unsubscribe        (E2kContext *ctx,
					      const char *uri);


/*
 * Utility functions
 */
SoupMessage   *e2k_soup_message_new      (E2kContext *ctx,
					  const char *uri,
					  const char *method);
SoupMessage   *e2k_soup_message_new_full (E2kContext *ctx,
					  const char *uri,
					  const char *method,
					  const char *content_type,
					  SoupOwnership owner,
					  const char *body,
					  gulong length);
void           e2k_context_queue_message (E2kContext *ctx,
					  SoupMessage *msg, 
					  SoupMessageCallbackFn callback, 
					  gpointer user_data);
E2kHTTPStatus  e2k_context_send_message  (E2kContext *ctx,
					  E2kOperation *op,
					  SoupMessage *msg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_CONTEXT_H__ */

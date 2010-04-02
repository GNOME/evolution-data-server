
#ifndef CAMEL_NNTP_NEWSRC_H
#define CAMEL_NNTP_NEWSRC_H

#include <stdio.h>
#include "glib.h"

G_BEGIN_DECLS

typedef struct CamelNNTPNewsrc CamelNNTPNewsrc;

gint              camel_nntp_newsrc_get_highest_article_read   (CamelNNTPNewsrc *newsrc, const gchar *group_name);
gint              camel_nntp_newsrc_get_num_articles_read      (CamelNNTPNewsrc *newsrc, const gchar *group_name);
void             camel_nntp_newsrc_mark_article_read          (CamelNNTPNewsrc *newsrc,
							       const gchar *group_name, gint num);
void             camel_nntp_newsrc_mark_range_read            (CamelNNTPNewsrc *newsrc,
							       const gchar *group_name, glong low, glong high);

gboolean         camel_nntp_newsrc_article_is_read            (CamelNNTPNewsrc *newsrc,
							       const gchar *group_name, glong num);

gboolean         camel_nntp_newsrc_group_is_subscribed        (CamelNNTPNewsrc *newsrc, const gchar *group_name);
void             camel_nntp_newsrc_subscribe_group            (CamelNNTPNewsrc *newsrc, const gchar *group_name);
void             camel_nntp_newsrc_unsubscribe_group          (CamelNNTPNewsrc *newsrc, const gchar *group_name);

GPtrArray*       camel_nntp_newsrc_get_subscribed_group_names (CamelNNTPNewsrc *newsrc);
GPtrArray*       camel_nntp_newsrc_get_all_group_names        (CamelNNTPNewsrc *newsrc);
void             camel_nntp_newsrc_free_group_names           (CamelNNTPNewsrc *newsrc, GPtrArray *group_names);

void             camel_nntp_newsrc_write_to_file              (CamelNNTPNewsrc *newsrc, FILE *fp);
void             camel_nntp_newsrc_write                      (CamelNNTPNewsrc *newsrc);
CamelNNTPNewsrc *camel_nntp_newsrc_read_for_server            (const gchar *server);

G_END_DECLS

#endif /* CAMEL_NNTP_NEWSRC_H */

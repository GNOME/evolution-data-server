
#ifndef __E_BOOK_QUERY_H__
#define __E_BOOK_QUERY_H__

#include <libebook/e-contact.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_QUERY (e_book_query_get_type ())

typedef struct EBookQuery EBookQuery;

typedef enum {
  E_BOOK_QUERY_IS,
  E_BOOK_QUERY_CONTAINS,
  E_BOOK_QUERY_BEGINS_WITH,
  E_BOOK_QUERY_ENDS_WITH

  /*
    Consider these "coming soon".

    E_BOOK_QUERY_LT,
    E_BOOK_QUERY_LE,
    E_BOOK_QUERY_GT,
    E_BOOK_QUERY_GE,
    E_BOOK_QUERY_EQ,
  */
} EBookQueryTest;

EBookQuery* e_book_query_from_string  (const gchar *query_string);
gchar *       e_book_query_to_string    (EBookQuery *q);

EBookQuery* e_book_query_ref          (EBookQuery *q);
void        e_book_query_unref        (EBookQuery *q);

EBookQuery* e_book_query_and          (gint nqs, EBookQuery **qs, gboolean unref);
EBookQuery* e_book_query_andv         (EBookQuery *q, ...);
EBookQuery* e_book_query_or           (gint nqs, EBookQuery **qs, gboolean unref);
EBookQuery* e_book_query_orv          (EBookQuery *q, ...);

EBookQuery* e_book_query_not          (EBookQuery *q, gboolean unref);

EBookQuery* e_book_query_field_exists (EContactField   field);
EBookQuery* e_book_query_vcard_field_exists (const gchar *field);
EBookQuery* e_book_query_field_test   (EContactField   field,
				       EBookQueryTest     test,
				       const gchar        *value);
EBookQuery* e_book_query_vcard_field_test (const gchar    *field,
				       EBookQueryTest     test,
				       const gchar        *value);

/* a special any field contains query */
EBookQuery* e_book_query_any_field_contains (const gchar  *value);

GType       e_book_query_get_type (void);
EBookQuery* e_book_query_copy     (EBookQuery *q);

G_END_DECLS

#endif /* __E_BOOK_QUERY_H__ */

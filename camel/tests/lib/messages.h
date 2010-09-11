#include <camel/camel.h>

/* how many ways to set the content contents */
#define SET_CONTENT_WAYS (5)

/* messages.c */
CamelMimeMessage *test_message_create_simple (void);
void test_message_set_content_simple (CamelMimePart *part, gint how, const gchar *type, const gchar *text, gint len);
gint test_message_write_file (CamelMimeMessage *msg, const gchar *name);
CamelMimeMessage *test_message_read_file (const gchar *name);
gint test_message_compare_content (CamelDataWrapper *dw, const gchar *text, gint len);
gint test_message_compare (CamelMimeMessage *msg);

void test_message_dump_structure (CamelMimeMessage *m);

gint test_message_compare_header (CamelMimeMessage *m1, CamelMimeMessage *m2);
gint test_message_compare_messages (CamelMimeMessage *m1, CamelMimeMessage *m2);

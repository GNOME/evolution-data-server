
#include <camel/camel.h>

/* check the total/unread is what we think it should be, everywhere it can be determined */
void test_folder_counts(CamelFolder *folder, gint total, gint unread);
/* cross-check info/msg */
void test_message_info(CamelMimeMessage *msg, const CamelMessageInfo *info);
/* check a message is present everywhere it should be */
void test_folder_message(CamelFolder *folder, const gchar *uid);
/* check message not present everywhere it shouldn't be */
void test_folder_not_message(CamelFolder *folder, const gchar *uid);
/* test basic folder ops on a store */
void test_folder_basic(CamelSession *session, const gchar *storename, gint local, gint spool);
/* test basic message operations on a folder */
void test_folder_message_ops(CamelSession *session, const gchar *storename, gint local, const gchar *foldername);

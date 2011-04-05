

#ifndef UTILS_H
#define UTILS_H

G_BEGIN_DECLS

typedef enum {
	EMAIL_DEBUG_FOLDER=1,
	EMAIL_DEBUG_STORE=2,
	EMAIL_DEBUG_SESSION=3,
	EMAIL_DEBUG_IPC=4,
	EMAIL_DEBUG_MICRO=5
} EMailDebugFlag;

void mail_debug_init ();
gboolean mail_debug_log (EMailDebugFlag flag);

G_END_DECLS

#endif /* UTILS_H */


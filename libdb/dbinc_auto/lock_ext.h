/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_lock_ext_h_
#define	_lock_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __lock_id __P((DB_ENV *, u_int32_t *));
gint __lock_id_free __P((DB_ENV *, u_int32_t));
gint __lock_vec __P((DB_ENV *, u_int32_t, u_int32_t, DB_LOCKREQ *, int, DB_LOCKREQ **));
gint __lock_get __P((DB_ENV *, u_int32_t, u_int32_t, const DBT *, db_lockmode_t, DB_LOCK *));
gint  __lock_put __P((DB_ENV *, DB_LOCK *));
gint __lock_downgrade __P((DB_ENV *, DB_LOCK *, db_lockmode_t, u_int32_t));
gint __lock_addfamilylocker __P((DB_ENV *, u_int32_t, u_int32_t));
gint __lock_freefamilylocker  __P((DB_LOCKTAB *, u_int32_t));
gint __lock_set_timeout __P(( DB_ENV *, u_int32_t, db_timeout_t, u_int32_t));
gint __lock_inherit_timeout __P(( DB_ENV *, u_int32_t, u_int32_t));
gint __lock_getlocker __P((DB_LOCKTAB *, u_int32_t, u_int32_t, int, DB_LOCKER **));
gint __lock_promote __P((DB_LOCKTAB *, DB_LOCKOBJ *, u_int32_t));
gint __lock_expired __P((DB_ENV *, db_timeval_t *, db_timeval_t *));
gint __lock_detect __P((DB_ENV *, u_int32_t, u_int32_t, gint *));
void __lock_dbenv_create __P((DB_ENV *));
void __lock_dbenv_close __P((DB_ENV *));
gint __lock_open __P((DB_ENV *));
gint __lock_dbenv_refresh __P((DB_ENV *));
void __lock_region_destroy __P((DB_ENV *, REGINFO *));
gint __lock_id_set __P((DB_ENV *, u_int32_t, u_int32_t));
gint __lock_stat __P((DB_ENV *, DB_LOCK_STAT **, u_int32_t));
gint __lock_dump_region __P((DB_ENV *, gchar *, FILE *));
void __lock_printlock __P((DB_LOCKTAB *, struct __db_lock *, int));
gint __lock_cmp __P((const DBT *, DB_LOCKOBJ *));
gint __lock_locker_cmp __P((u_int32_t, DB_LOCKER *));
u_int32_t __lock_ohash __P((const DBT *));
u_int32_t __lock_lhash __P((DB_LOCKOBJ *));
u_int32_t __lock_locker_hash __P((u_int32_t));

#if defined(__cplusplus)
}
#endif
#endif /* !_lock_ext_h_ */

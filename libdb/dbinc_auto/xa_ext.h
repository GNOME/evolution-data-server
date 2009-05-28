/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_xa_ext_h_
#define	_xa_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __db_xa_create __P((DB *));
gint __db_rmid_to_env __P((gint rmid, DB_ENV **envp));
gint __db_xid_to_txn __P((DB_ENV *, XID *, size_t *));
gint __db_map_rmid __P((int, DB_ENV *));
gint __db_unmap_rmid __P((int));
gint __db_map_xid __P((DB_ENV *, XID *, size_t));
void __db_unmap_xid __P((DB_ENV *, XID *, size_t));

#if defined(__cplusplus)
}
#endif
#endif /* !_xa_ext_h_ */

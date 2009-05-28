/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_tcl_ext_h_
#define	_tcl_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint bdb_HCommand __P((Tcl_Interp *, int, Tcl_Obj * CONST*));
#if DB_DBM_HSEARCH != 0
gint bdb_NdbmOpen __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DBM **));
#endif
#if DB_DBM_HSEARCH != 0
gint bdb_DbmCommand __P((Tcl_Interp *, int, Tcl_Obj * CONST*, int, DBM *));
#endif
gint ndbm_Cmd __P((ClientData, Tcl_Interp *, int, Tcl_Obj * CONST*));
void _DbInfoDelete __P((Tcl_Interp *, DBTCL_INFO *));
gint db_Cmd __P((ClientData, Tcl_Interp *, int, Tcl_Obj * CONST*));
gint dbc_Cmd __P((ClientData, Tcl_Interp *, int, Tcl_Obj * CONST*));
gint env_Cmd __P((ClientData, Tcl_Interp *, int, Tcl_Obj * CONST*));
gint tcl_EnvRemove __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *, DBTCL_INFO *));
gint tcl_EnvVerbose __P((Tcl_Interp *, DB_ENV *, Tcl_Obj *, Tcl_Obj *));
gint tcl_EnvAttr __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_EnvTest __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
DBTCL_INFO *_NewInfo __P((Tcl_Interp *, gpointer , gchar *, enum INFOTYPE));
gpointer _NameToPtr __P((CONST gchar *));
DBTCL_INFO *_PtrToInfo __P((CONST gpointer ));
DBTCL_INFO *_NameToInfo __P((CONST gchar *));
void  _SetInfoData __P((DBTCL_INFO *, gpointer ));
void  _DeleteInfo __P((DBTCL_INFO *));
gint _SetListElem __P((Tcl_Interp *, Tcl_Obj *, gpointer , int, gpointer , int));
gint _SetListElemInt __P((Tcl_Interp *, Tcl_Obj *, gpointer , int));
gint _SetListRecnoElem __P((Tcl_Interp *, Tcl_Obj *, db_recno_t, u_char *, int));
gint _Set3DBTList __P((Tcl_Interp *, Tcl_Obj *, DBT *, int, DBT *, int, DBT *));
gint _SetMultiList __P((Tcl_Interp *, Tcl_Obj *, DBT *, DBT*, int, int));
gint _GetGlobPrefix __P((gchar *, gchar **));
gint _ReturnSetup __P((Tcl_Interp *, int, int, gchar *));
gint _ErrorSetup __P((Tcl_Interp *, int, gchar *));
void _ErrorFunc __P((CONST gchar *, gchar *));
gint _GetLsn __P((Tcl_Interp *, Tcl_Obj *, DB_LSN *));
gint _GetUInt32 __P((Tcl_Interp *, Tcl_Obj *, u_int32_t *));
Tcl_Obj *_GetFlagsList __P((Tcl_Interp *, u_int32_t, void (*)(u_int32_t, gpointer , void (*)(u_int32_t, const FN *, gpointer ))));
void _debug_check  __P((void));
gint _CopyObjBytes  __P((Tcl_Interp *, Tcl_Obj *obj, gpointer *, u_int32_t *, gint *));
gint tcl_LockDetect __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LockGet __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LockStat __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LockTimeout __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LockVec __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LogArchive __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LogCompare __P((Tcl_Interp *, int, Tcl_Obj * CONST*));
gint tcl_LogFile __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LogFlush __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LogGet __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LogPut __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_LogStat __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint logc_Cmd __P((ClientData, Tcl_Interp *, int, Tcl_Obj * CONST*));
void _MpInfoDelete __P((Tcl_Interp *, DBTCL_INFO *));
gint tcl_MpSync __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_MpTrickle __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_Mp __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *, DBTCL_INFO *));
gint tcl_MpStat __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_RepElect __P((Tcl_Interp *, int, Tcl_Obj * CONST *, DB_ENV *));
gint tcl_RepFlush __P((Tcl_Interp *, int, Tcl_Obj * CONST *, DB_ENV *));
gint tcl_RepLimit __P((Tcl_Interp *, int, Tcl_Obj * CONST *, DB_ENV *));
gint tcl_RepRequest __P((Tcl_Interp *, int, Tcl_Obj * CONST *, DB_ENV *));
gint tcl_RepStart __P((Tcl_Interp *, int, Tcl_Obj * CONST *, DB_ENV *));
gint tcl_RepProcessMessage __P((Tcl_Interp *, int, Tcl_Obj * CONST *, DB_ENV *));
gint tcl_RepStat __P((Tcl_Interp *, int, Tcl_Obj * CONST *, DB_ENV *));
void _TxnInfoDelete __P((Tcl_Interp *, DBTCL_INFO *));
gint tcl_TxnCheckpoint __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_Txn __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *, DBTCL_INFO *));
gint tcl_TxnStat __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_TxnTimeout __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *));
gint tcl_TxnRecover __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *, DBTCL_INFO *));
gint bdb_RandCommand __P((Tcl_Interp *, int, Tcl_Obj * CONST*));
gint tcl_Mutex __P((Tcl_Interp *, int, Tcl_Obj * CONST*, DB_ENV *, DBTCL_INFO *));

#if defined(__cplusplus)
}
#endif
#endif /* !_tcl_ext_h_ */

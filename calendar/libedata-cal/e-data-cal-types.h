
#ifndef __E_DATA_CAL_TYPES_H__
#define __E_DATA_CAL_TYPES_H__

G_BEGIN_DECLS

typedef enum {
	Success,
	RepositoryOffline,
	PermissionDenied,
	InvalidRange,
	ObjectNotFound,
	InvalidObject,
	ObjectIdAlreadyExists,
	AuthenticationFailed,
	AuthenticationRequired,
	UnsupportedField,
	UnsupportedMethod,
	UnsupportedAuthenticationMethod,
	TLSNotAvailable,
	NoSuchCal,
	UnknownUser,
	OfflineUnavailable,

	/* These can be returned for successful searches, but
		indicate the result set was truncated */
	SearchSizeLimitExceeded,
	SearchTimeLimitExceeded,

	InvalidQuery,
	QueryRefused,

	CouldNotCancel,

	OtherError,
	InvalidServerVersion

} EDataCalCallStatus;

/* Some hacks so the backends compile without change */
/* TODO: Find out how many of these are necessary */
#define GNOME_Evolution_Calendar_CallStatus EDataCalCallStatus
#define GNOME_Evolution_Calendar_Success Success
#define GNOME_Evolution_Calendar_RepositoryOffline RepositoryOffline
#define GNOME_Evolution_Calendar_PermissionDenied PermissionDenied
#define GNOME_Evolution_Calendar_InvalidRange InvalidRange
#define GNOME_Evolution_Calendar_ObjectNotFound ObjectNotFound
#define GNOME_Evolution_Calendar_InvalidObject InvalidObject
#define GNOME_Evolution_Calendar_ObjectIdAlreadyExists ObjectIdAlreadyExists
#define GNOME_Evolution_Calendar_AuthenticationFailed AuthenticationFailed
#define GNOME_Evolution_Calendar_AuthenticationRequired AuthenticationRequired
#define GNOME_Evolution_Calendar_UnsupportedField UnsupportedField
#define GNOME_Evolution_Calendar_UnsupportedMethod UnsupportedMethod
#define GNOME_Evolution_Calendar_UnsupportedAuthenticationMethod UnsupportedAuthenticationMethod
#define GNOME_Evolution_Calendar_TLSNotAvailable TLSNotAvailable
#define GNOME_Evolution_Calendar_NoSuchCal NoSuchCal
#define GNOME_Evolution_Calendar_UnknownUser UnknownUser
#define GNOME_Evolution_Calendar_OfflineUnavailable OfflineUnavailable
#define GNOME_Evolution_Calendar_SearchSizeLimitExceeded SearchSizeLimitExceeded
#define GNOME_Evolution_Calendar_SearchTimeLimitExceeded SearchTimeLimitExceeded
#define GNOME_Evolution_Calendar_InvalidQuery InvalidQuery
#define GNOME_Evolution_Calendar_QueryRefused QueryRefused
#define GNOME_Evolution_Calendar_CouldNotCancel CouldNotCancel
#define GNOME_Evolution_Calendar_OtherError OtherError
#define GNOME_Evolution_Calendar_InvalidServerVersion InvalidServerVersion

typedef enum {
	Set,                    /* All OK */
	NotSet,                /* Generic error */
	NotSupported           /* Mode not supported */
} EDataCalViewListenerSetModeStatus;

#define GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED NotSupported
#define GNOME_Evolution_Calendar_CalListener_MODE_NOT_SET NotSet
#define GNOME_Evolution_Calendar_CalListener_MODE_SET Set

typedef enum {
	Event = 1 << 0,
	Todo = 1 << 1,
	Journal = 1 << 2,
	AnyType = 0x07
} EDataCalObjType;

#define GNOME_Evolution_Calendar_CalObjType EDataCalObjType

typedef enum {
	This = 1 << 0,
	ThisAndPrior = 1 << 1,
	ThisAndFuture = 1 << 2,
	All = 0x07
} EDataCalObjModType;

typedef enum {
	Local = 1 << 0,
	Remote = 1 << 1,
	AnyMode = 0x07
} EDataCalMode;

#define GNOME_Evolution_Calendar_CalMode EDataCalMode
#define GNOME_Evolution_Calendar_MODE_LOCAL Local
#define GNOME_Evolution_Calendar_MODE_REMOTE Remote
#define GNOME_Evolution_Calendar_MODE_ANY AnyMode

G_END_DECLS

#endif /* __E_DATA_CAL_TYPES_H__ */

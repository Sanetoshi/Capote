// Tiny Sound Capture Object "Capote"

#pragma once

class ICapote
{
public:
	typedef enum {
		ERR_OK					=  0,
		ERR_NODEVICE			= -1,
		ERR_INITDSCAP			= -2,
		ERR_CREATEDSCAPBUF		= -3,
		ERR_INITDSCAPNOTIFIER	= -4,
		ERR_CAPSTART			= -5,
		ERR_MMIOOPEN			= -6,
		ERR_UNKNOWN				= 0xffffffff,
	} ErrCode;

public:
	virtual ~ICapote() {}

public:
	virtual bool IsEnabled(void) = 0;
	virtual ICapote::ErrCode Start(const char *pFilename) = 0;
	virtual int Stop(void) = 0;

public:
	static ICapote * _cdecl Create(void);
};

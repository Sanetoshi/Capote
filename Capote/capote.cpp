// Tiny Sound Capture Object "Capote"

#include "capote.h"
#include <windows.h>
#include <dsound.h>
#include <vector>

#define CAPOTE_MAX(a, b)	( (a) > (b) ? (a) : (b) )
#define CAPOTE_RELEASE(p)	{ if(p) { (p)->Release(); (p)=NULL; } }

class CCapote : public ICapote
{
private:
	bool				m_bEnabled;
	bool				m_bAlive;
	LONG				m_nWrittenBytes;
	WAVEFORMATEX		m_fmtWav;

public:
	CCapote(void);
	virtual ~CCapote(void);

public:
	bool IsEnabled(void);
	ICapote::ErrCode Start(const char *pFilename);
	int Stop(void);

private:
	static DWORD WINAPI CapLoop(LPVOID lpParameter);

private:	// for DirectSoundCapture
	const static DWORD			nRecNotify = 16;
	LPDIRECTSOUNDCAPTURE		m_pDsCap;
	LPDIRECTSOUNDCAPTUREBUFFER	m_pDsCapBuf;
	LPDIRECTSOUNDNOTIFY			m_pDsNotify;
	DSBPOSITIONNOTIFY			m_arrPosNotify[CCapote::nRecNotify + 1];  
	DWORD						m_nCapBufSize;
	DWORD						m_nNotifySize;
	DWORD						m_nNextCapOffset;
	GUID						*m_pDsCapGuid;
	HANDLE						m_hNotifyEvent;
	HANDLE						m_hThread;

	bool InitDsCap(void);
	void FinishDsCap(void);
	bool CreateDsCapBuf(void);
	bool InitDsCapNotifier(void);
	bool RecordDsCap(void);

private:	// for Enumerate DirectSoundCapture Devices
	const static DWORD	nMaxDevices = 20;
	GUID				m_guidDevices[CCapote::nMaxDevices];
	DWORD				m_nDevices;

	void EnumDsCapDev(void);
	static INT_PTR CALLBACK EnumDsCapDevCallback(GUID *pGuid, LPSTR strDesc, LPSTR strDrvName, VOID *pContext);

private:	// for MMIO
	HMMIO				m_hMmio;
	MMCKINFO			m_mmckRiff;
	MMCKINFO			m_mmckData;
	MMCKINFO			m_mmckFmt;
    MMIOINFO			m_mmioInfo;

	bool MmioOpen(const char *pFilename);
	LONG MmioWrite(char _huge *pBuf, LONG size);
	void MmioClose(void);
};

CCapote::CCapote(void)
: m_bEnabled(FALSE)
, m_bAlive(FALSE)
, m_nWrittenBytes(0)
, m_pDsCap(NULL)
, m_pDsCapBuf(NULL)
, m_pDsNotify(NULL)
, m_nCapBufSize(0)
, m_nNotifySize(0)
, m_nNextCapOffset(0)
, m_pDsCapGuid(NULL)
, m_hNotifyEvent(NULL)
, m_nDevices(0)
, m_hMmio(NULL)
{
	// Set WAVE format
	::ZeroMemory(&m_fmtWav, sizeof(WAVEFORMATEX));
	m_fmtWav.wFormatTag      = WAVE_FORMAT_PCM;
	m_fmtWav.nChannels       = 2;
	m_fmtWav.nSamplesPerSec  = 48000;
	m_fmtWav.wBitsPerSample  = 16;
	m_fmtWav.nBlockAlign     = m_fmtWav.wBitsPerSample / 8 * m_fmtWav.nChannels;
	m_fmtWav.nAvgBytesPerSec = m_fmtWav.nSamplesPerSec * m_fmtWav.nBlockAlign;

	this->EnumDsCapDev();
	if (m_nDevices > 0) m_bEnabled = TRUE;
}

CCapote::~CCapote(void)
{
}

bool CCapote::IsEnabled(void)
{
	return m_bEnabled;
}

ICapote::ErrCode CCapote::Start(const char *pFilename)
{
	if (this->IsEnabled() == FALSE)         return ICapote::ERR_NODEVICE;
	if (this->InitDsCap() == FALSE)         return ICapote::ERR_INITDSCAP;
	if (this->CreateDsCapBuf() == FALSE)    return ICapote::ERR_CREATEDSCAPBUF;
	if (this->InitDsCapNotifier() == FALSE) return ICapote::ERR_INITDSCAPNOTIFIER;
	if (this->MmioOpen(pFilename) == FALSE) return ICapote::ERR_MMIOOPEN;

	m_bAlive = TRUE;
	m_hThread = ::CreateThread(NULL, 0, &CCapote::CapLoop, (LPVOID)this, 0, NULL);
	if (FAILED(m_pDsCapBuf->Start(DSCBSTART_LOOPING))) return ICapote::ERR_CAPSTART;

	return ICapote::ERR_OK;
}

int CCapote::Stop(void)
{
	m_pDsCapBuf->Stop();
	for (;;) {
		DWORD ExitCode;
		m_bAlive = FALSE;
		::SetEvent(m_hNotifyEvent);
		Sleep(16);
		::GetExitCodeThread(m_hThread, &ExitCode);
		if (ExitCode != STILL_ACTIVE) break;
	}

	this->RecordDsCap();
	this->MmioClose();
	this->FinishDsCap();

	return (m_nWrittenBytes / m_fmtWav.nChannels) / (m_fmtWav.wBitsPerSample / 8);
}

DWORD WINAPI CCapote::CapLoop(LPVOID lpParameter)
{
	CCapote *pThis = (CCapote *)lpParameter;

	for (;;) {
		::WaitForSingleObject(pThis->m_hNotifyEvent, INFINITE);
		if (pThis->m_bAlive == FALSE) break;
		if (pThis->RecordDsCap() == FALSE) break;
	}

	return 0;
}

/*----------------------------------------------------------------------------*
 * // DirectSoundCapture Functions
 *----------------------------------------------------------------------------*/
bool CCapote::InitDsCap(void)
{
	::ZeroMemory(&m_arrPosNotify, sizeof(DSBPOSITIONNOTIFY) * (CCapote::nRecNotify + 1));
	m_nCapBufSize = 0;
	m_nNotifySize = 0;

	// Initialize COM
	if (FAILED(::CoInitialize(NULL))) return FALSE;

	// Create IDirectSoundCapture using the preferred capture device
	if (FAILED(DirectSoundCaptureCreate(m_pDsCapGuid, &m_pDsCap, NULL))) return FALSE;

	return TRUE;
}

void CCapote::FinishDsCap(void)
{
	// Release DirectSound interfaces
	CAPOTE_RELEASE(m_pDsNotify);
	CAPOTE_RELEASE(m_pDsCapBuf);
	CAPOTE_RELEASE(m_pDsCap); 

	// Release COM
	::CoUninitialize();
}

bool CCapote::CreateDsCapBuf(void)
{
	CAPOTE_RELEASE(m_pDsNotify);
	CAPOTE_RELEASE(m_pDsCapBuf);

	// Set the notification size
	m_nNotifySize = CAPOTE_MAX(1024, m_fmtWav.nAvgBytesPerSec / 8);
	m_nNotifySize -= m_nNotifySize % m_fmtWav.nBlockAlign;   

	// Set the buffer sizes 
	m_nCapBufSize = m_nNotifySize * CCapote::nRecNotify;

	// Create the capture buffer
	DSCBUFFERDESC descDsCapBuf;
	::ZeroMemory(&descDsCapBuf, sizeof(DSCBUFFERDESC));
	descDsCapBuf.dwSize        = sizeof(DSCBUFFERDESC);
	descDsCapBuf.dwBufferBytes = m_nCapBufSize;
	descDsCapBuf.lpwfxFormat   = &m_fmtWav;

	if (FAILED(m_pDsCap->CreateCaptureBuffer(&descDsCapBuf, &m_pDsCapBuf, NULL))) return FALSE;
	m_nNextCapOffset = 0;

    return TRUE;
}

bool CCapote::InitDsCapNotifier(void)
{
	if (m_pDsCap == NULL) return FALSE;

	// Create a notification event, for when the sound stops playing
	if (FAILED(m_pDsCapBuf->QueryInterface(IID_IDirectSoundNotify, (VOID**)&m_pDsNotify))) return FALSE;

	// Setup the notification positions
	m_hNotifyEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	for (DWORD i = 0; i < CCapote::nRecNotify; i++) {
		m_arrPosNotify[i].dwOffset = (m_nNotifySize * i) + m_nNotifySize - 1;
		m_arrPosNotify[i].hEventNotify = m_hNotifyEvent;             
	}

	// Tell DirectSound when to notify us. the notification will come in the from 
	// of signaled events that are handled in WinMain()
	if (FAILED(m_pDsNotify->SetNotificationPositions(CCapote::nRecNotify, m_arrPosNotify))) return FALSE;

	return TRUE;
}

bool CCapote::RecordDsCap(void)
{
	VOID *pData1, *pData2;
	DWORD nData1Len, nData2Len, posCap, posRead;
	LONG nLockSize;

	pData1 = pData2 = NULL;
	if (m_pDsCapBuf == NULL) return FALSE;
	if (FAILED(m_pDsCapBuf->GetCurrentPosition(&posCap, &posRead))) return FALSE;

	nLockSize = posRead - m_nNextCapOffset;
	if (nLockSize < 0) nLockSize += m_nCapBufSize;

	// Block align lock size so that we are always write on a boundary
	nLockSize -= (nLockSize % m_nNotifySize);
	if (nLockSize == 0) return FALSE;

	// Lock the capture buffer down
	if (FAILED(m_pDsCapBuf->Lock(m_nNextCapOffset, nLockSize, &pData1, &nData1Len, &pData2, &nData2Len, 0L))) return FALSE;

	// Write the data into the wav file
	this->MmioWrite((char _huge *)pData1, (LONG)nData1Len);

	// Move the capture offset along
	m_nNextCapOffset += nData1Len; 
	m_nNextCapOffset %= m_nCapBufSize; // Circular buffer

	if (pData2 != NULL)	{
		// Write the data into the wav file
		this->MmioWrite((char _huge *)pData2, (LONG)nData2Len);

		// Move the capture offset along
		m_nNextCapOffset += nData2Len; 
		m_nNextCapOffset %= m_nCapBufSize; // Circular buffer
	}

	// Unlock the capture buffer
	m_pDsCapBuf->Unlock(pData1, nData1Len, pData2, nData2Len);

	return TRUE;
}

/*----------------------------------------------------------------------------*
 * // Enumerate DirectSoundCapture Devices
 *----------------------------------------------------------------------------*/
void CCapote::EnumDsCapDev(void)
{
	DirectSoundCaptureEnumerate((LPDSENUMCALLBACK)CCapote::EnumDsCapDevCallback, (VOID *)this);
}

INT_PTR CCapote::EnumDsCapDevCallback(GUID *pGuid, LPSTR strDesc, LPSTR strDrvName, VOID *pContext)
{
	CCapote *pThis = static_cast<CCapote *>(pContext);
	if (pGuid != NULL) {
		if (pThis->m_nDevices >= CCapote::nMaxDevices) return TRUE;
		::CopyMemory((void *)&pThis->m_guidDevices[pThis->m_nDevices++], (const void *)pGuid, sizeof(GUID));
	}
	return TRUE;
}

/*----------------------------------------------------------------------------*
 * MMIO Functions
 *----------------------------------------------------------------------------*/
bool CCapote::MmioOpen(const char *pFilename)
{
	m_hMmio = mmioOpenA((LPSTR)pFilename, NULL, MMIO_ALLOCBUF | MMIO_CREATE | MMIO_READWRITE);
	if (m_hMmio == NULL) return FALSE;

	m_mmckRiff.fccType = mmioStringToFOURCCA("WAVE", 0);
	mmioCreateChunk(m_hMmio, &m_mmckRiff, MMIO_CREATERIFF);

	m_mmckFmt.ckid = mmioStringToFOURCCA("fmt ", 0);
	mmioCreateChunk(m_hMmio, &m_mmckFmt, 0);
    if (m_fmtWav.wFormatTag == WAVE_FORMAT_PCM) {
		mmioWrite(m_hMmio, (const char *)&m_fmtWav, sizeof(PCMWAVEFORMAT));
    } else {
		mmioWrite(m_hMmio, (const char *)&m_fmtWav, sizeof(m_fmtWav) + m_fmtWav.cbSize);
    }
	mmioAscend(m_hMmio, &m_mmckFmt, 0);

    // Now create the fact chunk, not required for PCM but nice to have.  This is filled
    // in when the close routine is called.
    MMCKINFO ckFact;
	DWORD nFact = -1;
    ckFact.ckid = mmioStringToFOURCCA("fact", 0);
	mmioCreateChunk(m_hMmio, &ckFact, 0);
	mmioWrite(m_hMmio, (const char *)&nFact, sizeof(DWORD));
	mmioAscend(m_hMmio, &ckFact, 0);

	m_mmckData.ckid = mmioStringToFOURCCA("data", 0);
	mmioCreateChunk(m_hMmio, &m_mmckData, 0);
	mmioGetInfo(m_hMmio, &m_mmioInfo, 0);

	return TRUE;
}

LONG CCapote::MmioWrite(char _huge *pBuf, LONG size)
{
	LONG nWritten = 0;
	for (LONG i = 0; i < size; i++) {
		if (m_mmioInfo.pchNext == m_mmioInfo.pchEndWrite) {
			m_mmioInfo.dwFlags |= MMIO_DIRTY;
			if (mmioAdvance(m_hMmio, &m_mmioInfo, MMIO_WRITE) != 0) {
				return -1;
			}
		}
		*((BYTE *)m_mmioInfo.pchNext) = *((BYTE *)pBuf + i);
		(BYTE *)m_mmioInfo.pchNext++;
		nWritten++;
	}

	m_nWrittenBytes += nWritten;
	return nWritten;
}

void CCapote::MmioClose(void)
{
    m_mmioInfo.dwFlags |= MMIO_DIRTY;
    mmioSetInfo(m_hMmio, &m_mmioInfo, 0);

	// Ascend the output file out of the 'data' chunk -- this will cause
    // the chunk size of the 'data' chunk to be written.
	mmioAscend(m_hMmio, &m_mmckData, 0);

	// Do this here instead...
	mmioAscend(m_hMmio, &m_mmckRiff, 0);
	mmioSeek(m_hMmio, 0, SEEK_SET);
	mmioDescend(m_hMmio, &m_mmckRiff, NULL, 0);
	m_mmckData.ckid = mmioStringToFOURCCA("fact", 0);
	if (mmioDescend(m_hMmio, &m_mmckData, &m_mmckRiff, MMIO_FINDCHUNK) == 0) {
		DWORD nSamples = 0;
		mmioWrite(m_hMmio, (const char *)&nSamples, sizeof(DWORD));
		mmioAscend(m_hMmio, &m_mmckData, 0);
	}

	// Ascend the output file out of the 'RIFF' chunk -- this will cause
	// the chunk size of the 'RIFF' chunk to be written.
	mmioAscend(m_hMmio, &m_mmckRiff, 0);

	mmioClose(m_hMmio, 0);
	m_hMmio = NULL;
}

/*----------------------------------------------------------------------------*
 * Create Instance
 *----------------------------------------------------------------------------*/
ICapote * ICapote::Create(void)
{
	return new CCapote;
}

/*----------------------------------------------------------------------------*
 * indefinite removing for waveIn functions
 *----------------------------------------------------------------------------*/
#if	0
	MMRESULT			m_retDev;
	HWAVEIN				m_hWavIn;
	UINT				m_idDev;
	WAVEHDR				m_hdrWav;
	DWORD				m_nRecSec;
	DWORD				m_nBuffers;
	std::vector<PBYTE>	m_listBuffers;
	std::vector<PBYTE>::iterator	m_itBuffer;
	void SetupSoundDevice(void);
	void ShutdownSoundDevice(void);
	void FreeBuffers(void);
	static void CALLBACK WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);

	::waveInAddBuffer(m_hWavIn, &m_hdrWav, sizeof(WAVEHDR));
	::waveInStart(m_hWavIn);
	::waveInStop(m_hWavIn);
	::waveInClose(m_hWavIn);

void CCapote::SetupSoundDevice(void)
{
	// Set WAVE format
	::ZeroMemory(&m_fmtWav, sizeof(WAVEFORMATEX));
	m_fmtWav.wFormatTag      = WAVE_FORMAT_PCM;
	m_fmtWav.nChannels       = 2;
	m_fmtWav.nSamplesPerSec  = 48000;
	m_fmtWav.wBitsPerSample  = 16;
	m_fmtWav.nBlockAlign     = m_fmtWav.wBitsPerSample / 8 * m_fmtWav.nChannels;
	m_fmtWav.nAvgBytesPerSec = m_fmtWav.nSamplesPerSec * m_fmtWav.nBlockAlign;

	// Create Buffers
	for (DWORD i = 0; i < m_nBuffers; i++) {
		PBYTE pTmp = (PBYTE)::HeapAlloc(::GetProcessHeap(), 0, m_hdrWav.dwBufferLength);
		if (pTmp == NULL) {
			this->FreeBuffers();
			return;
		}
		m_listBuffers.push_back(pTmp);
	}
	m_itBuffer = m_listBuffers.begin();
	::ZeroMemory(&m_hdrWav, sizeof(WAVEHDR));
	m_hdrWav.dwBufferLength = m_fmtWav.nAvgBytesPerSec * m_nRecSec;
	m_hdrWav.dwFlags        = 0;
	m_hdrWav.lpData         = (LPSTR)(*m_itBuffer);

	// Open Device & Prepare Header
	m_retDev = ::waveInOpen(&m_hWavIn, m_idDev, &m_fmtWav, (DWORD)CCapote::WaveInProc, (DWORD)this, CALLBACK_FUNCTION);
	if (m_retDev == MMSYSERR_NOERROR) {
		m_retDev = ::waveInPrepareHeader(m_hWavIn , &m_hdrWav , sizeof(WAVEHDR));
		if (m_retDev == MMSYSERR_NOERROR) {
			m_bEnabled = TRUE;
		}
	}
}

void CCapote::ShutdownSoundDevice(void)
{
	::waveInUnprepareHeader(m_hWavIn , &m_hdrWav , sizeof(WAVEHDR));
	::waveInClose(m_hWavIn);
	this->FreeBuffers();
}

void CCapote::WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	CCapote *pThis = (CCapote *)dwInstance;

	switch (uMsg)
	{
	case WIM_DATA:
		::waveInUnprepareHeader(pThis->m_hWavIn, &pThis->m_hdrWav, sizeof(WAVEHDR));
		pThis->MmioWrite();
		pThis->m_itBuffer++;
		if (pThis->m_itBuffer == pThis->m_listBuffers.end()) {
			pThis->m_itBuffer = pThis->m_listBuffers.begin();
		}
		pThis->m_hdrWav.lpData = (LPSTR)(*pThis->m_itBuffer);
		::waveInPrepareHeader(pThis->m_hWavIn, &pThis->m_hdrWav , sizeof(WAVEHDR));
		::waveInReset(pThis->m_hWavIn);
		break;
	case WIM_CLOSE:
	case WIM_OPEN:
	default:
		break;
	}
}

void CCapote::FreeBuffers(void)
{
	for (std::vector<PBYTE>::iterator it = m_listBuffers.begin(); it != m_listBuffers.end(); it++) {
		::HeapFree(::GetProcessHeap(), 0, *it);
	}
}
#endif

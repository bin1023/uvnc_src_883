#ifndef __CONNECTIONCONSOLE_H__
#define __CONNECTIONCONSOLE_H__

#include <winsock2.h>
#include <vector>
#include "ConnectionCommand.h"

#define MAX_HOST_NAME_LEN 256
#define G_SENDBUFFER 1452

#define FT_PROTO_VERSION_OLD 1  // <= RC18 server.. "fOldFTPRotocole" version
#define FT_PROTO_VERSION_2   2  // base ft protocol
#define FT_PROTO_VERSION_3   3  // new ft protocol session messages

class CZipUnZip32;
namespace rdr { class InStream; class FdInStream; class ZlibInStream; }

//////////////////////////////////////////////////////////////////////////////
// UltraVncViewerのClientConnection,FileTransferの処理をまとめてる
class ConnectionConsole {
public:
    ConnectionConsole();
    virtual ~ConnectionConsole();

    int Startup(PConsoleCmd open);
    int ExecCmd(int cmdno, PVOID para=NULL);
    int Cleanup();

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
protected:
    SOCKET m_sock;
    TCHAR m_host[MAX_HOST_NAME_LEN];
    TCHAR m_clearPasswd[256];
    int m_port;
    bool m_passwordfailed;
    rdr::FdInStream* fis;
    char m_QueueBuffer[G_SENDBUFFER + 1];
    DWORD m_nQueueBufferLength;
    DWORD m_LastSentTick;
    __int64 m_BytesSend;
    __int64 m_BytesRead;
    int m_nTO;
    int m_majorVersion;
    int m_minorVersion;
    bool new_ultra_server;
    BOOL m_fServerKnowsFileTransfer;
    char *m_netbuf;
    int m_netbufsize;
    rfbServerInitMsg m_si;
    TCHAR *m_desktopName;

    int m_ServerFTProtocolVersion;
    int m_nBlockSize;
    bool m_fFileCommandPending; // 現状：なんだか実施中ON,　今回：使わない予定
    bool m_fFTAllowed;  // 今回：使わない予定
    bool m_fFileDownloadRunning;
    bool m_fFileDownloadError;

    HANDLE m_hSrcFile;
    char m_szSrcFileName[MAX_PATH + 32];
    unsigned char *m_filechunkbuf;
    int m_filechunkbufsize = 0;
    DWORD m_dwNbBytesRead;
    __int64 m_dwTotalNbBytesRead;
    bool m_fEof;
    bool m_fFileUploadError;
    bool m_fFileUploadRunning;
    bool m_fSendFileChunk;
    bool m_fCompress;
    char* m_lpCSBuffer;
    int m_nCSOffset;
    int	m_nCSBufferSize;
    __int64	m_nnFileSize;
    DWORD m_dwCurrentValue;
    DWORD m_dwCurrentPercent;
    DWORD m_dwStartTick;
    CZipUnZip32 *m_pZipUnZip;
    unsigned char *m_filezipbuf;
    int m_filezipbufsize;
    
    // Directory list reception
    WIN32_FIND_DATA m_fd;
    int m_nFileCount;
    bool m_fDirectoryReceptionRunning;
    char m_szFileSpec[MAX_PATH + 64];

    //////////////////////////////////
    BOOL m_waitAnswer; // サーバからの応答を待つか
    char* m_szSrcFileNamee; // ファイル名のみ
    int m_cmd; // 実行中コマンド
    int m_ret;
    char* m_szRemoteFileName;

    /////////////////////////////////////////////////////////// 
    /////////////////////////////////////////////////////////// 
    int HandShake();
    void NegotiateProtocolVersion();
    void Authenticate(std::vector<CARD32>& current_auth);
    void AuthenticateServer(CARD32 authScheme, std::vector<CARD32>& current_auth);

    void ReadExact(char *inbuf, int wanted);
    void ReadExactProtocolVersion(char *inbuf, int wanted, bool& fNotEncrypted);
    void WriteExact(char *buf, int bytes);
    void WriteExact(char *buf, int bytes, CARD8 msgType);
    void WriteExactQueue(char *buf, int bytes);
    void WriteExactQueue(char *buf, int bytes, CARD8 msgType);
    void WriteTransformed(char *buf, int bytes, bool bQueue);
    void WriteTransformed(char *buf, int bytes, CARD8 msgType, bool bQueue);
    void FlushWriteQueue(bool bTimeout = false, int timeout = 0);
    void Write(char *buf, int bytes, bool bQueue, bool bTimeout = false, int timeout = 0);
    void FlushOutstandingWriteQueue(char*& buf2, int& bytes2, bool bTimeout = false, int timeout = 0);
    int Send(const char *buff, const unsigned int bufflen, int timeout);
    void CheckBufferSize(int bufsize);
    void ReadString(char *buf, int length);
    void AuthVnc();
    void SendClientInit();
    void ReadServerInit();

    ///
    void ProcessFileTransferMsg(void);
    void RequestPermission();
    bool TestPermission(long lSize, int nVersion);
    void StartFTSession();
    void EndFTSession();
    void RequestRemoteDrives();
    bool UsingOldProtocol() { return m_ServerFTProtocolVersion == FT_PROTO_VERSION_OLD; }
    void ListRemoteDrives(HWND hWnd, int nLen);
    int ZipPossibleDirectory(LPSTR szSrcFileName);
    bool ReceiveDestinationFileChecksums(int nSize, int nLen);
    bool MyGetFileSize(char* szFilePath, ULARGE_INTEGER *n2FileSize);
    bool OfferLocalFile(PVOID para);
    bool SetSendTimeout(int msecs = -1);
    bool SetRecvTimeout(int msecs = -1);
    void GetFriendlyFileSizeString(__int64 Size, char* szText);
    bool SendFile(long lSize, int nLen);
    bool SendFileChunk();
    void CheckFileChunkBufferSize(int bufsize);
    bool FinishFileSending();
    void CheckFileZipBufferSize(int bufsize);
    void SetGauge(__int64 dwCount);
    //bool FinishFileReception();

    // 
    void RequestRemoteDirectoryContent(LPTSTR szPath);
    void PopulateRemoteListBox(int nLen);
    void ReceiveDirectoryItem(int nLen);
    void FinishDirectoryReception();
    void CheckRemoteFile(LPTSTR szPath);
};

#endif //__CONNECTIONCONSOLE_H__
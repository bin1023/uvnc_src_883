/////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2019 BIN. All Rights Reserved.
//  Licensed under the GNU General Public License or
//  (at your option) any later version.
////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "ConnectionConsole.h"
#include "LogConsole.h"
#include "ExceptionConsole.h"
#include "MsgMgr.h"
#include "ConnectionCommand.h"
extern "C" {
#include "vncauth.h"
}
#include "zlib-1.2.5/zlib.h"
#include "ZipUnZip32/zipUnZip32.h"
extern LogConsole vnclog;

ConnectionConsole::ConnectionConsole() {
    m_port = 5900; // port固定
    m_passwordfailed = true;
    m_ServerFTProtocolVersion = FT_PROTO_VERSION_3;
    m_nBlockSize = sz_rfbBlockSize;
    m_fFileCommandPending = false;
    m_fFTAllowed = false;
    m_fFileDownloadRunning = false;
    m_fFileDownloadError = false;
    m_pZipUnZip = new CZipUnZip32();
    m_lpCSBuffer = NULL;
    m_nQueueBufferLength = 0;
    m_nTO = 1;

    m_filezipbuf = NULL;
    m_filezipbufsize = 0;
    m_filechunkbuf = NULL;
    m_filechunkbufsize = 0;
    fis = NULL;

    memset(m_szFileSpec, 0, sizeof m_szFileSpec);
    m_fDirectoryReceptionRunning = false;
    m_nFileCount = 0;

    m_ret = 0;
    m_waitAnswer = FALSE;
    m_szSrcFileNamee = NULL;
    m_szRemoteFileName = NULL;
}

ConnectionConsole::~ConnectionConsole() {
    if (m_pZipUnZip) delete m_pZipUnZip;
    if (m_lpCSBuffer) delete[] m_lpCSBuffer;
    if (fis) delete fis;
}

int ConnectionConsole::Startup(PConsoleCmd open){
    PCCOpenData opendata = (PCCOpenData)&open->data;
    _tcsncpy_s(m_host, opendata->host, min(_countof(m_host), _tcslen(opendata->host)));
    _tcsncpy_s(m_clearPasswd, opendata->pass, min(_countof(m_host), _tcslen(opendata->pass)));
    if (0 != opendata->port) m_port = opendata->port;

    WORD wVersionRequested = MAKEWORD(2, 0);
    WSADATA wsaData;
    int ret = WSAStartup(wVersionRequested, &wsaData);
    if ( ret != 0) {
        vnclog.Print(0, GETMSG(INIT_WSASTARTUP), ret);
        return INIT_WSASTARTUP;
    }

    struct sockaddr_in thataddr;
    if (m_sock != NULL && m_sock != INVALID_SOCKET) {
        // 来ないはず
        closesocket(m_sock);
    }
    m_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (m_sock == INVALID_SOCKET) {
        vnclog.Print(0, GETMSG(INIT_CREATE_SOCKET_ERR), WSAGetLastError());
        return INIT_CREATE_SOCKET_ERR;
    }

    thataddr.sin_addr.s_addr = inet_addr(m_host);
    if (thataddr.sin_addr.s_addr == INADDR_NONE) {
        LPHOSTENT lphost;
        lphost = gethostbyname(m_host);
        if (lphost == NULL){
            vnclog.Print(0, GETMSG(INIT_GETHOST_ERR), WSAGetLastError());
            return INIT_GETHOST_ERR;
        };
        thataddr.sin_addr.s_addr = ((LPIN_ADDR)lphost->h_addr)->s_addr;
    };

    thataddr.sin_family = AF_INET;
    thataddr.sin_port = htons(m_port);
    ret = connect(m_sock, (LPSOCKADDR)&thataddr, sizeof(thataddr));
    if (ret == SOCKET_ERROR) {
        // ??
        //if (WSA_INVALID_HANDLE == WSAGetLastError()) {
        //    Sleep(5000);
        //}
        vnclog.Print(0, GETMSG(INIT_CONNECT_ERR), WSAGetLastError());
        return INIT_CONNECT_ERR;
    }

    BOOL nodelayval = TRUE;
    if (setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelayval, sizeof(BOOL))) {
        vnclog.Print(0, GETMSG(INIT_SETSOCKET_ERR), WSAGetLastError());
        return INIT_SETSOCKET_ERR;
    }
    fis = new rdr::FdInStream(m_sock);
    fis->SetDSMMode(false);  // とりあえず
    
    return HandShake();
}

int ConnectionConsole::Cleanup(){
    EndFTSession();
    WSACleanup();
    return 0;
}

int ConnectionConsole::HandShake() {
    //  Client                      Server
    //    |------ Connect         -->>--|
    //    |--<<-- ProtocolVersion ------|
    //    |------ ProtocolVersion -->>--|
    //    |--<<-- Authenticate    ------|
    //    |------ VNCAuthenticate -->>--|
    //    |--<<-- Success         ------|
    //    |------ ClientInit      -->>--|
    //    |--<<-- ServerInit      ------|
    //    |                             |
    //    |------ PixelFormat等   -->>--|  (Consoleでは対応しない)

    NegotiateProtocolVersion();
    std::vector<CARD32> current_auth;
    Authenticate(current_auth);
    if (!m_fServerKnowsFileTransfer) {
        vnclog.Print(0, GETMSG(INIT_SERVER_OLD));
        return INIT_SERVER_OLD;
    }

    SendClientInit();
    ReadServerInit();
    return ExecCmd(CMD_REQPERM);
}

int ConnectionConsole::ExecCmd(int cmdno, PVOID para){
    try {
        m_cmd = cmdno;
        m_ret = 1; // 戻り値：デフォルトは失敗
        switch (cmdno) {
        case CMD_REQPERM:
            RequestPermission();
            break;
        case CMD_PUT:
            OfferLocalFile(para);
            break;
        case CMD_CHECK:
            CheckRemoteFile(LPTSTR(para));
            break;
        default:
            break;
        }

        // コマンドは応答待ち
        rdr::U8 msgType = 0;
        while (m_waitAnswer) {
            m_waitAnswer = FALSE;
            msgType = fis->readU8();
            switch (msgType) {
            case rfbKeepAlive:
                if (sz_rfbKeepAliveMsg > 1) {
                    rfbKeepAliveMsg kp;
                    ReadExact(((char *)&kp) + m_nTO, sz_rfbKeepAliveMsg - m_nTO);
                }
                break;
            case rfbRequestSession:
            case rfbFramebufferUpdate:
            case rfbSetColourMapEntries:
            case rfbBell:
            case rfbServerCutText:
            case rfbTextChat:
            case rfbResizeFrameBuffer:
            case rfbServerState:
            case rfbNotifyPluginStreaming:
            default:
                break;
            case rfbFileTransfer:
                ProcessFileTransferMsg();
                break;
            }
        }

        // 
        switch (cmdno) {
        case CMD_REQPERM:
            if (!m_fFTAllowed) {
                vnclog.Print(0, GETMSG(INIT_SERVER_OLD));
                return INIT_SERVER_OLD;
            } else {
                m_ret = 0;
            }
            break;
        default:
            break;
        }

    } catch (ExceptionConsole &c) {
        vnclog.Print(0, _T("Exception : %s\n"), c.m_info);
        m_ret = 91;
    } catch (rdr::Exception &e) {
        vnclog.Print(0, "rdr::Exception (1): %s\n", e.str());
        m_ret = 92;
    } catch (...){
        vnclog.Print(0, "Exception \n");
        m_ret = 93;
    }

    return m_ret;
}

void ConnectionConsole::NegotiateProtocolVersion(){
    rfbProtocolVersionMsg pv;
    bool fNotEncrypted = false;

    try {
        ReadExactProtocolVersion(pv, sz_rfbProtocolVersionMsg, fNotEncrypted);
    } catch (rdr::EndOfStream& c) {
        vnclog.Print(0, _T("Error reading protocol version: %s\n"), c.str());
        throw WarningExceptionConsole("Connection failed - End of Stream\r\n\r\n"
                               "Possible causes:\r\r"
                               "- Another user is already listening on this ID\r\n"
                               "- Bad connection\r\n", 1004
                               );
    } catch (ExceptionConsole &c) {
        vnclog.Print(0, _T("Error reading protocol version: %s\n"), c.m_info);
        throw WarningExceptionConsole("Connection failed - Error reading Protocol Version\r\n\r\n"
                               "Possible causes:\r\n"
                               "- You've forgotten to select a DSMPlugin and the Server uses a DSMPlugin\r\n"
                               "- Viewer and Server are not compatible (they use different RFB protocols)\r\n"
                               "- Bad connection\r\n", 1004
                               );
    }

    pv[sz_rfbProtocolVersionMsg] = 0;
    if (sscanf(pv, rfbProtocolVersionFormat, &m_majorVersion, &m_minorVersion) != 2) {
        throw WarningExceptionConsole("Connection failed - Invalid protocol !\r\n\r\n"
                               "Possible causes:\r\r"
                               "- You've forgotten to select a DSMPlugin and the Server uses a DSMPlugin\r\n"
                               "- Viewer and Server are not compatible (they use different RFB protocols)\r\n"
                               );
    }
    vnclog.Print(0, _T("RFB server supports protocol version %d.%d\n"), 
                 m_majorVersion, m_minorVersion);

    // ファイル転送できるかをチェック
    if (m_minorVersion == 4) {
        m_fServerKnowsFileTransfer = true;
    } else if (m_minorVersion == 6) {// 6 because 5 already used in TightVNC viewer for some reason
        m_fServerKnowsFileTransfer = true;
    } else if (m_minorVersion == 7) {// adzm 2010-09 - RFB 3.8
        // adzm2010-10 - RFB3.8 - m_fServerKnowsFileTransfer set during rfbUltraVNC auth
    } else if (m_minorVersion == 8) {// adzm 2010-09 - RFB 3.8
        // adzm2010-10 - RFB3.8 - m_fServerKnowsFileTransfer set during rfbUltraVNC auth
    } else if (m_minorVersion == 14) {
        // Added for SC so we can do something before actual data transfer start
        m_fServerKnowsFileTransfer = true;
    } else if (m_minorVersion == 16) {
        m_fServerKnowsFileTransfer = true;
    } else if (m_minorVersion == 18) {// adzm 2010-09 - RFB 3.8
        m_fServerKnowsFileTransfer = true;
    }
    else if ((m_majorVersion == 3) && (m_minorVersion < 3)) {
        /* if server is 3.2 we can't use the new authentication */
        vnclog.Print(0, _T("Can't use IDEA authentication\n"));
        /* This will be reported later if authentication is requested*/
    } else if ((m_majorVersion == 3) && (m_minorVersion == 3)) {
        /* if server is 3.2 we can't use the new authentication */
        vnclog.Print(0, _T("RFB version 3.3, Legacy \n"));
        /* This will be reported later if authentication is requested*/
    } else {
        /* any other server version, just tell the server what we want */
        m_majorVersion = rfbProtocolMajorVersion;
        m_minorVersion = rfbProtocolMinorVersion; // always 4 for Ultra Viewer
    }

    sprintf(pv, rfbProtocolVersionFormat, m_majorVersion, m_minorVersion);
    WriteExact(pv, sz_rfbProtocolVersionMsg);
    if (m_minorVersion == 14 || m_minorVersion == 16 || m_minorVersion == 18) { // adzm 2010-09
        int size;
        ReadExact((char *)&size, sizeof(int));
        char mytext[1025]; //10k
        //block
        if (size < 0 || size >1024)
        {
            if (size < 0) size = 0;
            if (size > 1024) size = 1024;
        }

        ReadExact(mytext, size);
        mytext[size] = 0;

        int nummer = 1;
        WriteExact((char *)&nummer, sizeof(int));
        m_minorVersion -= 10;
    }
    vnclog.Print(0, _T("Connected to RFB server, using protocol version %d.%d\n"),
                 rfbProtocolMajorVersion, m_minorVersion);

}
void ConnectionConsole::ReadExactProtocolVersion(char *inbuf, int wanted, bool& fNotEncrypted) {
    fNotEncrypted = false;
    fis->readBytes(inbuf, wanted);
}

void ConnectionConsole::Authenticate(std::vector<CARD32>& current_auth) {
    CARD32 authScheme = rfbInvalidAuth;
    if (m_minorVersion < 7) {
        ReadExact((char *)&authScheme, sizeof(authScheme));
        authScheme = Swap32IfLE(authScheme);

        // adzm 2010-10 - TRanslate legacy constants into new 3.8-era constants
        switch (authScheme) {
        case rfbLegacy_SecureVNCPlugin:
            authScheme = rfbUltraVNC_SecureVNCPluginAuth_new;
            break;
        case rfbLegacy_MsLogon:
            authScheme = rfbUltraVNC_MsLogonIIAuth;
            break;
        }
    }
    else {
        CARD8 authAllowedLength = 0;
        ReadExact((char *)&authAllowedLength, sizeof(authAllowedLength));
        if (authAllowedLength == 0) {
            authScheme = rfbConnFailed;
        }
        else {
            CARD8 authAllowed[256];
            ReadExact((char *)authAllowed, authAllowedLength);

            std::vector<CARD8> auth_supported;
            for (int i = 0; i < authAllowedLength; i++) {
                if (std::find(current_auth.begin(), current_auth.end(), (CARD32)authAllowed[i]) != current_auth.end()) {
                    // only once max per scheme
                    continue;
                }

                switch (authAllowed[i]) {
                case rfbUltraVNC:
                case rfbUltraVNC_SecureVNCPluginAuth:
                case rfbUltraVNC_SecureVNCPluginAuth_new:
                case rfbUltraVNC_SCPrompt: // adzm 2010-10
                case rfbUltraVNC_SessionSelect:
                case rfbUltraVNC_MsLogonIIAuth:
                case rfbVncAuth:
                case rfbNoAuth:
                    auth_supported.push_back(authAllowed[i]);
                    break;
                }
            }

            if (!auth_supported.empty()) {
                std::vector<CARD8> auth_priority;
                auth_priority.push_back(rfbUltraVNC);
                auth_priority.push_back(rfbUltraVNC_SecureVNCPluginAuth_new);
                auth_priority.push_back(rfbUltraVNC_SecureVNCPluginAuth);
                auth_priority.push_back(rfbUltraVNC_SCPrompt); // adzm 2010-10
                auth_priority.push_back(rfbUltraVNC_SessionSelect);
                auth_priority.push_back(rfbUltraVNC_MsLogonIIAuth);
                auth_priority.push_back(rfbVncAuth);
                auth_priority.push_back(rfbNoAuth);

                for (std::vector<CARD8>::iterator best_auth_it = auth_priority.begin(); best_auth_it != auth_priority.end(); best_auth_it++) {
                    if (std::find(auth_supported.begin(), auth_supported.end(), (CARD32)(*best_auth_it)) != auth_supported.end()) {
                        authScheme = *best_auth_it;
                        break;
                    }
                }
            }

            if (authScheme == rfbInvalidAuth) {
                //throw WarningExceptionConsole("No supported authentication methods!");
            }

            CARD8 authSchemeMsg = (CARD8)authScheme;
            WriteExact((char *)&authSchemeMsg, sizeof(authSchemeMsg));
        }
    }

    AuthenticateServer(authScheme, current_auth);
}

void ConnectionConsole::AuthenticateServer(CARD32 authScheme, std::vector<CARD32>& current_auth) {
    if (current_auth.size() > 5) {
        vnclog.Print(0, _T("Cannot layer more than two authentication schemes\n"), authScheme);
        throw ErrorExceptionConsole("Cannot layer more than two authentication schemes\n");
    }

    CARD32 reasonLen;
    CARD32 authResult;

    bool bSecureVNCPluginActive = std::find(current_auth.begin(), current_auth.end(), rfbUltraVNC_SecureVNCPluginAuth) != current_auth.end();
    if (!bSecureVNCPluginActive) bSecureVNCPluginActive = std::find(current_auth.begin(), current_auth.end(), rfbUltraVNC_SecureVNCPluginAuth_new) != current_auth.end();

    switch (authScheme) {
    case rfbUltraVNC:
        new_ultra_server = true;
        m_fServerKnowsFileTransfer = true;
        break;
#if 0
    case rfbUltraVNC_SecureVNCPluginAuth_new:
        if (bSecureVNCPluginActive) {
            vnclog.Print(0, _T("Cannot layer multiple SecureVNC plugin authentication schemes\n"), authScheme);
            throw WarningExceptionConsole("Cannot layer multiple SecureVNC plugin authentication schemes\n");
        }
        AuthSecureVNCPlugin();
        break;
    case rfbUltraVNC_SecureVNCPluginAuth:
        if (bSecureVNCPluginActive) {
            vnclog.Print(0, _T("Cannot layer multiple SecureVNC plugin authentication schemes\n"), authScheme);
            throw WarningExceptionConsole("Cannot layer multiple SecureVNC plugin authentication schemes\n");
        }
        AuthSecureVNCPlugin_old();
        break;
    case rfbUltraVNC_MsLogonIIAuth:
        AuthMsLogonII();
        break;
    case rfbUltraVNC_MsLogonIAuth:
        m_ms_logon_I_legacy = true;
#endif
    case rfbVncAuth:
        AuthVnc();
        break;
    case rfbUltraVNC_SCPrompt:
        //AuthSCPrompt();
        break;
    case rfbUltraVNC_SessionSelect:
        //AuthSessionSelect();
        break;
    case rfbNoAuth:
        vnclog.Print(0, _T("No authentication needed\n"));
        //		current_auth.push_back(authScheme);
        if (m_minorVersion < 8) {
            current_auth.push_back(authScheme);
            return;
        }
        break;
    case rfbConnFailed:
        ReadExact((char *)&reasonLen, 4);
        reasonLen = Swap32IfLE(reasonLen);

        CheckBufferSize(reasonLen + 1);
        ReadString(m_netbuf, reasonLen);

        vnclog.Print(0, _T("RFB connection failed, reason: %s\n"), m_netbuf);
        throw WarningExceptionConsole(m_netbuf);
        break;
    default:
        vnclog.Print(0, _T("RFB connection failed, unknown authentication method: %lu\n"), authScheme);
        throw WarningExceptionConsole("Unknown authentication method!");
        break;
    }

    current_auth.push_back(authScheme);

    // Read the authentication response
    ReadExact((char *)&authResult, sizeof(authResult));
    authResult = Swap32IfLE(authResult);

    switch (authResult) {
    case rfbVncAuthOK:
        vnclog.Print(0, _T("VNC authentication succeeded\n"));
        break;
    case rfbVncAuthFailed:
    case rfbVncAuthFailedEx:
        vnclog.Print(0, _T("VNC authentication failed!"));
        if (m_minorVersion >= 7 || authResult == rfbVncAuthFailedEx) {
            vnclog.Print(0, _T("VNC authentication failed! Extended information available."));
            //adzm 2010-05-11 - Send an explanatory message for the failure (if any)
            ReadExact((char *)&reasonLen, 4);
            reasonLen = Swap32IfLE(reasonLen);

            CheckBufferSize(reasonLen + 1);
            ReadString(m_netbuf, reasonLen);

            vnclog.Print(0, _T("VNC authentication failed! Extended information: %s\n"), m_netbuf);
            throw WarningExceptionConsole(m_netbuf);
        }
        else {
            vnclog.Print(0, _T("VNC authentication failed!"));
            throw WarningExceptionConsole("VNC authentication failed!");
        }
        break;
    case rfbVncAuthTooMany:
        throw WarningExceptionConsole("VNC authentication failed - too many tries!");
        break;
#if 0
    case rfbLegacy_MsLogon:
        if (m_minorVersion >= 7) {
            vnclog.Print(0, _T("Invalid auth response for protocol version.\n"));
            throw ErrorExceptionConsole("Invalid auth response");
        }
        if ((authScheme != rfbUltraVNC_SecureVNCPluginAuth) || !m_pIntegratedPluginInterface) {
            vnclog.Print(0, _T("Invalid auth response response\n"));
            throw ErrorExceptionConsole("Invalid auth response");
        }
        //adzm 2010-05-10
        AuthMsLogonII();
        break;
#endif
    case rfbVncAuthContinue:
        if (m_minorVersion < 7) {
            vnclog.Print(0, _T("Invalid auth continue response for protocol version.\n"));
            throw ErrorExceptionConsole("Invalid auth continue response");
        }
        if (current_auth.size() > 5) { // arbitrary
            vnclog.Print(0, _T("Cannot layer more than six authentication schemes\n"), authScheme);
            throw ErrorExceptionConsole("Cannot layer more than six authentication schemes");
        }
        Authenticate(current_auth);
        break;
    default:
        vnclog.Print(0, _T("Unknown VNC authentication result: %d\n"), (int)authResult);
        throw ErrorExceptionConsole("Unknown VNC authentication result!");
        break;
    }

    return;
}

void ConnectionConsole::AuthVnc() {
    CARD8 challenge[CHALLENGESIZE];
    CARD32 authResult = rfbVncAuthFailed;
    if ((m_majorVersion == 3) && (m_minorVersion < 3)) {
        /* if server is 3.2 we can't use the new authentication */
        vnclog.Print(0, _T("Can't use IDEA authentication\n"));
        throw WarningExceptionConsole("Can't use IDEA authentication any more!");
    }
    // rdv@2002 - v1.1.x
    char passwd[256];
    memset(passwd, 0, sizeof(char) * 256);
    // Was the password already specified in a config file or entered for DSMPlugin ?
    // Modif sf@2002 - A clear password can be transmitted via the vncviewer command line
    if (strlen(m_clearPasswd) > 0) {
        strcpy(passwd, m_clearPasswd);
    }
    else {
        throw WarningExceptionConsole("パスワード未指定!");
    }
    ReadExact((char *)challenge, CHALLENGESIZE);
    vncEncryptBytes(challenge, passwd);
    /* Lose the plain-text password from memory */
    int nLen = (int)strlen(passwd);
    for (int i = 0; i < nLen; i++) {
        passwd[i] = '\0';
    }
    WriteExact((char *)challenge, CHALLENGESIZE);
}

void ConnectionConsole::SendClientInit() {
    rfbClientInitMsg ci;
    // adzm 2010-09
    ci.flags = 0;
    //if (m_opts.m_Shared) {
    ci.flags |= clientInitShared;
    //}

    WriteExact((char *)&ci, sz_rfbClientInitMsg); // sf@2002 - RSM Plugin
}

void ConnectionConsole::ReadServerInit() {
    // 取得しているが、実は使わない
    ReadExact((char *)&m_si, sz_rfbServerInitMsg);
    m_si.framebufferWidth = Swap16IfLE(m_si.framebufferWidth);
    m_si.framebufferHeight = Swap16IfLE(m_si.framebufferHeight);
    m_si.format.redMax = Swap16IfLE(m_si.format.redMax);
    m_si.format.greenMax = Swap16IfLE(m_si.format.greenMax);
    m_si.format.blueMax = Swap16IfLE(m_si.format.blueMax);
    m_si.nameLength = Swap32IfLE(m_si.nameLength);
    m_desktopName = new TCHAR[m_si.nameLength + 4 + 256];

#ifdef UNDER_CE
    char *deskNameBuf = new char[m_si.nameLength + 4];

    ReadString(deskNameBuf, m_si.nameLength);

    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED,
        deskNameBuf, m_si.nameLength,
        m_desktopName, m_si.nameLength + 1);
    delete deskNameBuf;
#else
    ReadString(m_desktopName, m_si.nameLength);
#endif
    vnclog.Print(0, _T("Desktop name \"%s\"\n"), m_desktopName);
    vnclog.Print(0, _T("Geometry %d x %d depth %d\n"),
        m_si.framebufferWidth, m_si.framebufferHeight, m_si.format.depth);
}

//
//	ProcessFileTransferMsg
//
//  Here we process all incoming FileTransferMsg stuff
//  coming from the server.
//  The server only sends FileTransfer data when requested
//  by the client. Possible request are:
//
//  - Send the list of your drives
//  - Send the content of a directory
//  - Send a file
//  - Accept a file
//  - ...
// 
//  We use the main ClientConnection thread and its
//  rfb message reception loop.
//  This function is called by the rfb message processing thread.
//  Thus it's safe to call the ReadExact and ReadString 
//  functions in the functions that are called from here:
//  PopulateRemoteListBox, ReceiveFile
// 
void ConnectionConsole::ProcessFileTransferMsg(void) {
    //	vnclog.Print(0, _T("ProcessFileTransferMsg\n"));
    rfbFileTransferMsg ft;
    ReadExact(((char *)&ft) + m_nTO, sz_rfbFileTransferMsg - m_nTO);

    switch (ft.contentType) {
        // Response to a rfbDirContentRequest request:
        // some directory data is received from the server
    case rfbFileTransferProtocolVersion: {
        int proto_ver = ft.contentParam;
        if ((proto_ver >= FT_PROTO_VERSION_OLD) && (proto_ver <= FT_PROTO_VERSION_3))
            m_ServerFTProtocolVersion = proto_ver;
        }
        break;
    case rfbDirPacket:
        switch (ft.contentParam) {
            // Response to a rfbRDrivesList request
        case rfbADrivesList:
            ListRemoteDrives(NULL, Swap32IfLE(ft.length));
            m_fFileCommandPending = false;
            break;

            // Response to a rfbRDirContent request 
        case rfbADirectory:
        case rfbAFile:
            if (!m_fDirectoryReceptionRunning)
                PopulateRemoteListBox(Swap32IfLE(ft.length));
            else
                ReceiveDirectoryItem(Swap32IfLE(ft.length));
            break;
        default: // This is bad. Add rfbADirectoryEnd instead...
            if (m_fDirectoryReceptionRunning) {
                // FinishDirectoryReception
                m_fDirectoryReceptionRunning = false;
                m_fFileCommandPending = false;
            }
            break;

        }
        break;

        // In response to a rfbFileTransferRequest request
        // A file is received from the server.
    case rfbFileHeader:
        //ReceiveFiles(Swap32IfLE(ft.size), Swap32IfLE(ft.length));
        break;

        // In response to a rfbFileTransferOffer request
        // The server can send the checksums of the destination file before sending a ack through
        // rfbFileAcceptHeader (only if the destination file already exists and is accessible)
    case rfbFileChecksums:
        ReceiveDestinationFileChecksums(Swap32IfLE(ft.size), Swap32IfLE(ft.length));
        SetRecvTimeout();
        m_waitAnswer = TRUE;
        break;

        // In response to a rfbFileTransferOffer request
        // A ack or nack is received from the server.
    case rfbFileAcceptHeader:
        SendFile(Swap32IfLE(ft.size), Swap32IfLE(ft.length));
        break;

        // Response to a command
    case rfbCommandReturn:
        switch (ft.contentParam) {
        case rfbADirCreate:
            //CreateRemoteDirectoryFeedback(Swap32IfLE(ft.size), Swap32IfLE(ft.length));
            m_fFileCommandPending = false;
            break;

        case rfbAFileDelete:
            //DeleteRemoteFileFeedback(Swap32IfLE(ft.size), Swap32IfLE(ft.length));
            m_fFileCommandPending = false;
            break;

        case rfbAFileRename:
            //RenameRemoteFileOrDirectoryFeedback(Swap32IfLE(ft.size), Swap32IfLE(ft.length));
            m_fFileCommandPending = false;
            break;
        }
        break;

        // Should never be handled here but in the File Transfer Loop
    case rfbFilePacket:
        //SetRecvTimeout();
        //ReceiveFileChunk(Swap32IfLE(ft.length), Swap32IfLE(ft.size));
        // adzm 2010-09
        //SendKeepAlive(false, true);
        break;

        // Should never be handled here but in the File Transfer Loop
    case rfbEndOfFile:
        //FinishFileReception();
        break;

        // Abort current file transfer
        // For versions <=RC18 we also use it to test if we're allowed to use FileTransfer on the server
    case rfbAbortFileTransfer:

        if (m_fFileDownloadRunning) {
            m_fFileDownloadError = true;
            //FinishFileReception();
        }
        else {
            // We want the viewer to be backward compatible with UltraWinVNC running the old FT protocole
            m_ServerFTProtocolVersion = FT_PROTO_VERSION_OLD; // Old permission method -> it's a <=RC18 server
            m_nBlockSize = 4096; // Old packet size value...

            TestPermission(Swap32IfLE(ft.size), 0);
        }

        break;

        // New FT handshaking/permission method (from RC19)
    case rfbFileTransferAccess:
        TestPermission(Swap32IfLE(ft.size), ft.contentParam);
        break;

    default:
        return;
        break;
    }
}

void ConnectionConsole::RequestPermission() {
    rfbFileTransferMsg ft;
    ft.type = rfbFileTransfer;
    // Versions <= RC18 method
    ft.contentType = rfbAbortFileTransfer;
    // ft.contentParam = 0; 
    ft.contentParam = rfbFileTransferVersion; // Old viewer will send 0
    // New method can't be used yet as we want backward compatibility (new viewer FT must 
    // work with old UltraWinVNC FT
    // ft.contentType = rfbFileTransferAccess; 
    // ft.contentParam = rfbFileTransferVersion;
    ft.length = 0;
    ft.size = 0;
    WriteExact((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);
    m_waitAnswer = TRUE;
    return;
}
bool ConnectionConsole::TestPermission(long lSize, int nVersion) {
    //	vnclog.Print(0, _T("TestPermission\n"));
    if (lSize == -1) {
        m_fFTAllowed = false;
    }
    else {
        m_fFTAllowed = true;
        StartFTSession();
        //RequestRemoteDrives();
    }

    return true;
}
void ConnectionConsole::StartFTSession() {
    if (m_ServerFTProtocolVersion < FT_PROTO_VERSION_3)
        return;

    rfbFileTransferMsg ft;
    memset(&ft, 0, sizeof ft);
    ft.type = rfbFileTransfer;
    ft.contentType = rfbFileTransferSessionStart;
    WriteExact((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);

    //m_waitAnswer = TRUE;
    //本コマンドは応答なし
}
void ConnectionConsole::EndFTSession() {
    if (m_ServerFTProtocolVersion < FT_PROTO_VERSION_3)
        return;

    rfbFileTransferMsg ft;
    memset(&ft, 0, sizeof ft);
    ft.type = rfbFileTransfer;
    ft.contentType = rfbFileTransferSessionEnd;
    WriteExact((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);
}

//
// request the list of remote drives 
//
void ConnectionConsole::RequestRemoteDrives() {
    //	vnclog.Print(0, _T("RequestRemoteDrives\n"));
    if (!m_fFTAllowed) return;

    // TODO : hook error !
    rfbFileTransferMsg ft;
    ft.type = rfbFileTransfer;
    ft.contentType = rfbDirContentRequest;
    ft.contentParam = rfbRDrivesList; // List of Remote Drives please
    ft.length = 0;
    WriteExact((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);

    return;
}

void ConnectionConsole::ListRemoteDrives(HWND hWnd, int nLen) {
    //	vnclog.Print(0, _T("ListRemoteDrives\n"));
    TCHAR szDrivesList[256]; // Format when filled : "C:t<NULL>D:t<NULL>....Z:t<NULL><NULL>"
    TCHAR szDrive[4];
    TCHAR szType[32];
    int nIndex = 0;

    if (nLen > sizeof(szDrivesList)) return;
    ReadString((char *)szDrivesList, nLen);
    // Fill the tree with the remote drives
    while (nIndex < nLen - 3) {
        strcpy(szDrive, szDrivesList + nIndex);
        nIndex += 4;

        // Get the type of drive
        switch (szDrive[2]) {
        case 'l':
            sprintf(szType, "%s", "Local Disk");
            break;
        case 'f':
            sprintf(szType, "%s", "Removable");
            break;
        case 'c':
            sprintf(szType, "%s", "CD-ROM");
            break;
        case 'n':
            sprintf(szType, "%s", "Network");
            break;
        default:
            sprintf(szType, "%s", "Unknown");
            break;
        }

        szDrive[2] = '\0'; // remove the type char
        printf("%s%s%s\t%s", rfbDirPrefix, szDrive, rfbDirSuffix, szType);

    }

    // List the usual shorcuts
    if (!UsingOldProtocol()) {
        // MyDocuments
        printf("%s%s%s", rfbDirPrefix, "My Documents", rfbDirSuffix);
        // Desktop
        printf("%s%s%s", rfbDirPrefix, "Desktop", rfbDirSuffix);
        printf("%s%s%s", rfbDirPrefix, "Network Favorites", rfbDirSuffix);
    }
}

//
// Offer a file
//
bool ConnectionConsole::OfferLocalFile(PVOID para) {
    if (NULL == para) return false;
    PCCPutData putdata = (PCCPutData)para;
    LPSTR szSrcFileName = putdata->from;
    LPSTR szDestFolder = putdata->to;
    strcpy(m_szSrcFileName, szSrcFileName);

    // sf@2003 - Directory Transfer trick
    // The File to transfer is actually a directory, so we must Zip it recursively and send
    // the resulting zip file (it will be recursively unzipped on server side once
    // the transfer is done)
    int nDirZipRet = ZipPossibleDirectory(m_szSrcFileName);
    if (nDirZipRet == -1) {
        m_fFileUploadError = true;
        return false;
    }

    // Open local src file
    m_hSrcFile = CreateFile(m_szSrcFileName, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (m_hSrcFile == INVALID_HANDLE_VALUE) {
        m_fFileUploadError = true;
        DWORD TheError = GetLastError();
        return false;
    }

    // Size of src file
    ULARGE_INTEGER n2SrcSize;
    bool bSize = MyGetFileSize(m_szSrcFileName, &n2SrcSize);
    // if (dwSrcSize == -1)
    if (!bSize) {
        CloseHandle(m_hSrcFile);
        m_fFileUploadError = true;
        return false;
    }

    char szFFS[96];
    GetFriendlyFileSizeString(n2SrcSize.QuadPart, szFFS);

    m_nnFileSize = n2SrcSize.QuadPart;
    //SetGauge(hWnd, 0); // In bytes

    // Add the File Time Stamp to the filename
    FILETIME SrcFileModifTime;
    BOOL fRes = GetFileTime(m_hSrcFile, NULL, NULL, &SrcFileModifTime);
    if (!fRes) {
        CloseHandle(m_hSrcFile);
        m_fFileUploadError = true;
        return false;
    }

    m_szSrcFileNamee = strrchr(m_szSrcFileName, '\\');
    if (NULL == m_szSrcFileNamee) { 
        m_szSrcFileNamee = m_szSrcFileName;
    } else {
        m_szSrcFileNamee++;
    }
    CloseHandle(m_hSrcFile);
    TCHAR szDstFileName[MAX_PATH + 32];
    memset(szDstFileName, 0, MAX_PATH + 32);
    strcpy_s(szDstFileName, sizeof(szDstFileName), szDestFolder);
    strcat(szDstFileName, strrchr(m_szSrcFileName, '\\') + 1);
    char szSrcFileTime[18];
    // sf@2003
    // For now, we've made the choice off displaying all the files 
    // off client AND server sides converted in clients local
    // time only. We keep file time as it is before transfering the file (absolute time)
    /*
    FILETIME LocalFileTime;
    FileTimeToLocalFileTime(&SrcFileModifTime, &LocalFileTime);
    */
    SYSTEMTIME FileTime;
    FileTimeToSystemTime(/*&LocalFileTime*/&SrcFileModifTime, &FileTime);
    wsprintf(szSrcFileTime, "%2.2d/%2.2d/%4.4d %2.2d:%2.2d",
        FileTime.wMonth, FileTime.wDay, FileTime.wYear, FileTime.wHour, FileTime.wMinute);
    strcat(szDstFileName, ",");
    strcat(szDstFileName, szSrcFileTime);

    // sf@2004 - Delta Transfer
    if (m_lpCSBuffer != NULL) {
        delete[] m_lpCSBuffer;
        m_lpCSBuffer = NULL;
    }
    m_nCSOffset = 0;
    m_nCSBufferSize = 0;

    // Send the FileTransferMsg with rfbFileTransferOffer
    // So the server creates the appropriate new file on the other side
    rfbFileTransferMsg ft;

    ft.type = rfbFileTransfer;
    ft.contentType = rfbFileTransferOffer;
    ft.contentParam = 0;
    ft.size = Swap32IfLE(n2SrcSize.LowPart); // File Size in bytes
    ft.length = Swap32IfLE(strlen(szDstFileName));
    //adzm 2010-09
    WriteExactQueue((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);
    WriteExactQueue((char *)szDstFileName, strlen(szDstFileName));

    if (!UsingOldProtocol()) {
        CARD32 sizeH = Swap32IfLE(n2SrcSize.HighPart);
        WriteExact((char *)&sizeH, sizeof(CARD32));
    }
    else {
        FlushWriteQueue();
    }

    SetSendTimeout();
    m_waitAnswer = true;
    m_ret = 0;
    return true;
}
//
// Zip a possible directory
//
int ConnectionConsole::ZipPossibleDirectory(LPSTR szSrcFileName) {
    char* p1 = strrchr(szSrcFileName, '\\') + 1;
    char* p2 = strrchr(szSrcFileName, rfbDirSuffix[0]);
    if (
        p1[0] == rfbDirPrefix[0] && p1[1] == rfbDirPrefix[1]  // Check dir prefix
        && p2[1] == rfbDirSuffix[1] && p2 != NULL && p1 < p2  // Check dir suffix
        ) //
    {
        // sf@2004 - Improving Directory Transfer: Avoids ReadOnly media problem
        char szDirZipPath[MAX_PATH];
        char szWorkingDir[MAX_PATH];
        ::GetTempPath(MAX_PATH, szWorkingDir); //PGM Use Windows Temp folder
        if (szWorkingDir == NULL) //PGM 
        { //PGM
            if (GetModuleFileName(NULL, szWorkingDir, MAX_PATH)) {
                char* p = strrchr(szWorkingDir, '\\');
                if (p == NULL)
                    return -1;
                *(p + 1) = '\0';
            }
            else {
                return -1;
            }
        }//PGM

        char szPath[MAX_PATH];
        char szDirectoryName[MAX_PATH];
        strcpy(szPath, szSrcFileName);
        p1 = strrchr(szPath, '\\') + 1;
        strcpy(szDirectoryName, p1 + 2); // Skip dir prefix (2 chars)
        szDirectoryName[strlen(szDirectoryName) - 2] = '\0'; // Remove dir suffix (2 chars)
        *p1 = '\0';
        if ((strlen(szPath) + strlen(rfbZipDirectoryPrefix) + strlen(szDirectoryName) + 4) > (MAX_PATH - 1)) return false;
        // sprintf(szSrcFileName, "%s%s%s%s", szPath, rfbZipDirectoryPrefix, szDirectoryName, ".zip"); 
        sprintf(szDirZipPath, "%s%s%s%s", szWorkingDir, rfbZipDirectoryPrefix, szDirectoryName, ".zip");
        strcat(szPath, szDirectoryName);
        strcpy(szDirectoryName, szPath);
        if (strlen(szDirectoryName) > (MAX_PATH - 4)) return -1;
        strcat(szDirectoryName, "\\*.*");
        bool fZip = m_pZipUnZip->ZipDirectory(szPath, szDirectoryName, szDirZipPath/*szSrcFileName*/, true);
        if (!fZip) return -1;
        strcpy(szSrcFileName, szDirZipPath);
        return 1;
    }
    else
        return 0;
}
//
// sf@2004 - Delta Transfer
// Destination file already exists
// The server sends the checksums of this file in one shot.
// 
bool ConnectionConsole::ReceiveDestinationFileChecksums(int nSize, int nLen){
    //	vnclog.Print(0, _T("ReceiveDestinationFileChecksums\n"));
    m_lpCSBuffer = new char[nLen + 1]; //nSize
    if (m_lpCSBuffer == NULL) {
        return false;
    }

    // char szStatus[255];
    // sprintf(szStatus, " Receiving %d bytes of file checksums from remote machine. Please wait...", nLen); 
    // SetStatus(szStatus);

    memset(m_lpCSBuffer, '\0', nLen + 1); // nSize

    ReadExact((char *)m_lpCSBuffer, nLen);
    m_nCSBufferSize = nLen;

    return true;
}

//
// GetFileSize() doesn't handle files > 4GBytes...
// GetFileSizeEx() doesn't exist under Win9x...
// So let's write our own function.
// 
bool ConnectionConsole::MyGetFileSize(char* szFilePath, ULARGE_INTEGER *n2FileSize) {
    WIN32_FIND_DATA fd;
    HANDLE ff;

    SetErrorMode(SEM_FAILCRITICALERRORS); // No popup please !
    ff = FindFirstFile(szFilePath, &fd);
    SetErrorMode(0);

    if (ff == INVALID_HANDLE_VALUE) {
        return false;
    }

    FindClose(ff);

    (*n2FileSize).LowPart = fd.nFileSizeLow;
    (*n2FileSize).HighPart = fd.nFileSizeHigh;
    (*n2FileSize).QuadPart = (((__int64)fd.nFileSizeHigh) << 32) + fd.nFileSizeLow;

    return true;
}
bool ConnectionConsole::SetSendTimeout(int msecs) {
    //int timeout = msecs < 0 ? m_opts.m_FTTimeout * 1000 : msecs;
    int timeout = msecs < 0 ? FT_RECV_TIMEOUT * 1000 : msecs;
    if (setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        return false;
    }
    return true;
}
bool ConnectionConsole::SetRecvTimeout(int msecs) {
    int timeout = msecs < 0 ? FT_RECV_TIMEOUT * 1000 : msecs;
    if (setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

//
// Format file size so it is user friendly to read
// 
void ConnectionConsole::GetFriendlyFileSizeString(__int64 Size, char* szText) {
    szText[0] = '\0';
    if (Size > (1024 * 1024 * 1024))
    {
        __int64 lRest = (Size % (1024 * 1024 * 1024));
        Size /= (1024 * 1024 * 1024);
        wsprintf(szText, "%u.%2.2lu Gb", (unsigned long)Size, (unsigned long)(lRest * 100 / 1024 / 1024 / 1024));
    }
    else if (Size > (1024 * 1024))
    {
        unsigned long lRest = (Size % (1024 * 1024));
        Size /= (1024 * 1024);
        wsprintf(szText, "%u.%2.2lu Mb", (unsigned long)Size, lRest * 100 / 1024 / 1024);
    }
    else if (Size > 1024)
    {
        unsigned long lRest = Size % (1024);
        Size /= 1024;
        wsprintf(szText, "%u.%2.2lu Kb", (unsigned long)Size, lRest * 100 / 1024);
    }
    else
    {
        wsprintf(szText, "%u bytes", (unsigned long)Size);
    }
}
//
//  SendFile 
// 
bool ConnectionConsole::SendFile(long lSize, int nLen) {
    if (nLen == 0) return false; // Used when the local file could no be open in OfferLocalFile

    if (NULL != m_szRemoteFileName) {
        delete[]m_szRemoteFileName;
    }
    m_szRemoteFileName = new char[nLen + 1];
    if (m_szRemoteFileName == NULL) return false;
    memset(m_szRemoteFileName, 0, nLen + 1);

    // Read in the Name of the file to copy (remote full name !)
    ReadExact(m_szRemoteFileName, nLen);

    if (nLen > MAX_PATH)
        m_szRemoteFileName[MAX_PATH] = '\0';
    else
        m_szRemoteFileName[nLen] = '\0';

    // If lSize = -1 (0xFFFFFFFF) that means that the Dst file on the remote machine
    // could not be created for some reason (locked..)
    if (lSize == -1) {
        m_fFileUploadError = true;

        delete[] m_szRemoteFileName;
        return false;
    }

    // Open src file
    m_hSrcFile = CreateFile(m_szSrcFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (m_hSrcFile == INVALID_HANDLE_VALUE)
    {
        DWORD TheError = GetLastError();
        m_fFileUploadError = true;
        return false;
    }

    m_fFileUploadError = false;
    m_dwNbBytesRead = 0;
    m_dwTotalNbBytesRead = 0;
    m_fEof = false;

    // If the connection speed is > 2048 Kbit/s, no need to compress.
    // VNCViewerではReadScreenUpdateの中で計算している。
    // fis->startTiming();
    // fis->stopTiming();
    // kbitsPerSecond = fis->kbitsPerSecond();
    m_fCompress = true;

    m_fFileUploadRunning = true;
    m_fFileUploadError = false;

    // Viewerの場合、メッセージキューを考慮する必要がある、
    //m_dwLastChunkTime = GetTickCount();
    m_dwStartTick = GetTickCount();

    do {
        if (m_fEof || m_fFileUploadError) {
            FinishFileSending();
            break;
        }
        SendFileChunk();
    } while (true);

    return true;
}

void ConnectionConsole::CheckFileChunkBufferSize(int bufsize)
{
    // sf@2009 - Sanity check
    if (bufsize < 0 || bufsize > 104857600) { // 100 MBytes max
        vnclog.Print(0, _T("Insufficient memory to allocate zlib buffer.\n"));
        throw ErrorExceptionConsole("Insufficient memory to allocate zlib buffer.");
    }

    unsigned char *newbuf;
    if (m_filechunkbufsize > bufsize) return;
    newbuf = (unsigned char *)new char[bufsize + 256];
    if (newbuf == NULL) {
        throw ErrorExceptionConsole("Insufficient memory to allocate zlib buffer.");
    }

    if (m_filechunkbuf != NULL)
        delete[] m_filechunkbuf;
    m_filechunkbuf = newbuf;
    m_filechunkbufsize = bufsize + 256;
    vnclog.Print(4, _T("m_filechunkbufsize expanded to %d\n"), m_filechunkbufsize);
}

void ConnectionConsole::CheckFileZipBufferSize(int bufsize)
{
    // sf@2009 - Sanity check
    if (bufsize < 0 || bufsize > 104857600) { // 100 MBytes max
        vnclog.Print(0, _T("Insufficient memory to allocate zlib buffer.\n"));
        throw ErrorExceptionConsole("Insufficient memory to allocate zlib buffer.");
    }

    unsigned char *newbuf;
    if (m_filezipbufsize > bufsize) return;
    newbuf = (unsigned char *)new char[bufsize + 256];
    if (newbuf == NULL) {
        throw ErrorExceptionConsole("Insufficient memory to allocate zlib buffer.");
    }

    // Only if we're successful...
    if (m_filezipbuf != NULL)
        delete[] m_filezipbuf;
    m_filezipbuf = newbuf;
    m_filezipbufsize = bufsize + 256;
    vnclog.Print(4, _T("zipbufsize expanded to %d\n"), m_filezipbufsize);
}

//
// Send the next file packet (upload)
// This function is called asynchronously from the
// main ClientConnection message loop
// 
bool ConnectionConsole::SendFileChunk()
{
    //	vnclog.Print(0, _T("SendFilechunk\n"));
    if (!m_fFileUploadRunning) return false;

    CheckFileChunkBufferSize(m_nBlockSize + 1024);
    int nRes = ReadFile(m_hSrcFile, m_filechunkbuf, m_nBlockSize, &m_dwNbBytesRead, NULL);
    if (!nRes && m_dwNbBytesRead != 0) {
        m_fFileUploadError = true;
    }

    if (nRes && m_dwNbBytesRead == 0) {
        m_fEof = true;
    }
    else {
        // sf@2004 - Delta Transfer
        bool fAlreadyThere = false;
        unsigned long nCS = 0;
        // if Checksums are available for this file
        if (m_lpCSBuffer != NULL) {
            if (m_nCSOffset < m_nCSBufferSize) {
                memcpy(&nCS, &m_lpCSBuffer[m_nCSOffset], 4);
                if (nCS != 0) {
                    m_nCSOffset += 4;
                    unsigned long cs = adler32(0L, Z_NULL, 0);
                    cs = adler32(cs, m_filechunkbuf, (int)m_dwNbBytesRead);
                    if (cs == nCS) {
                        fAlreadyThere = true;
                    }
                }
            }
        }

        if (fAlreadyThere) {
            // Send the FileTransferMsg with empty rfbFilePacket
            rfbFileTransferMsg ft;
            ft.type = rfbFileTransfer;
            ft.contentType = rfbFilePacket;
            ft.size = Swap32IfLE(2); // Means "Empty packet"// Swap32IfLE(nCS); 
            ft.length = Swap32IfLE(m_dwNbBytesRead);
            WriteExact((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);
            // m_nNotSent += m_dwNbBytesRead;
        }
        else
        {
            // Compress the data
            // (Compressed data can be longer if it was already compressed)
            unsigned int nMaxCompSize = m_nBlockSize + 1024; // TODO: Improve this...
            bool fCompressed = false;
            if (m_fCompress && !UsingOldProtocol()) {
                CheckFileZipBufferSize(nMaxCompSize);
                int nRetC = compress((unsigned char*)(m_filezipbuf),
                    (unsigned long*)&nMaxCompSize,
                    (unsigned char*)m_filechunkbuf,
                    m_dwNbBytesRead
                    );
                if (nRetC != 0) {
                    // Todo: send data uncompressed instead
                    m_fFileUploadError = true;
                    return false;
                }
                Sleep(5);
                fCompressed = true;
            }

            // If data compressed is larger, we're presumably dealing with already compressed data.
            if (nMaxCompSize > m_dwNbBytesRead)
                fCompressed = false;
            // m_fCompress = false;

            // Send the FileTransferMsg with rfbFilePacket
            rfbFileTransferMsg ft;
            ft.type = rfbFileTransfer;
            ft.contentType = rfbFilePacket;
            ft.size = fCompressed ? Swap32IfLE(1) : Swap32IfLE(0);
            ft.length = fCompressed ? Swap32IfLE(nMaxCompSize) : Swap32IfLE(m_dwNbBytesRead);
            //adzm 2010-09
            if (UsingOldProtocol())
                WriteExactQueue((char *)&ft, sz_rfbFileTransferMsg);
            else
                WriteExactQueue((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);

            if (fCompressed)
                WriteExact((char *)m_filezipbuf, nMaxCompSize);
            else
                WriteExact((char *)m_filechunkbuf, m_dwNbBytesRead);
        }

        m_dwTotalNbBytesRead += m_dwNbBytesRead;

        // Refresh progress bar
        SetGauge(m_dwTotalNbBytesRead);

        //if (m_fAbort) {
        //    m_fFileUploadError = true;
        //    FinishFileSending();
        //    return false;
        //}
    }

    return true;
}

bool ConnectionConsole::FinishFileSending() {
    //	vnclog.Print(0, _T("FinishSendFile\n"));
    if (!m_fFileUploadRunning) return false;

    m_fFileUploadRunning = false;
    SetSendTimeout(0);

    CloseHandle(m_hSrcFile);

    if (!m_fFileUploadError || m_fEof)
    {
        rfbFileTransferMsg ft;

        ft.type = rfbFileTransfer;
        ft.contentType = rfbEndOfFile;
        if (UsingOldProtocol())
            WriteExact((char *)&ft, sz_rfbFileTransferMsg);
        else
            WriteExact((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);

    }
    else // Error during file transfer loop
    {
        rfbFileTransferMsg ft;
        ft.type = rfbFileTransfer;
        ft.contentType = rfbAbortFileTransfer;
        ft.contentParam = rfbFileTransferVersion;
        ft.length = 0;
        ft.size = 0;
        if (UsingOldProtocol())
            WriteExact((char *)&ft, sz_rfbFileTransferMsg);
        else
            WriteExact((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);
        //sprintf(szStatus, " %s < %s > %s", sz_H19, m_szSrcFileName, sz_H28);
    }

    // If the transfered file is a Directory zip, we delete it locally, whatever the result of the transfer
    if (!strncmp(strrchr(m_szSrcFileName, '\\') + 1, rfbZipDirectoryPrefix, strlen(rfbZipDirectoryPrefix)))
    {
        DeleteFile(m_szSrcFileName);
        if (!m_fFileUploadError)
        {
            char szDirectoryName[MAX_PATH];
            char *p = strrchr(m_szSrcFileName, '\\');
            char *p1 = strchr(p, '-');
            strcpy(szDirectoryName, p1 + 1);
            szDirectoryName[strlen(szDirectoryName) - 4] = '\0'; // Remove '.zip'
            //sprintf(szStatus, " %s < %s > %s", sz_H66, szDirectoryName, sz_H70);
        }
    }

    printf("\r\n"); // 改行

    // 
    TCHAR szFolderPath[MAX_PATH] = { _T('\0') };
    strcpy(szFolderPath, m_szRemoteFileName);
    char *filename = strrchr(szFolderPath, '\\');
    if (filename){
        *filename = _T('\0');
        RequestRemoteDirectoryContent(szFolderPath);
    }
    return true;
}

/*
//
// Finish File Download
//
bool ConnectionConsole::FinishFileReception() {
    //	vnclog.Print(0, _T("FinishFileReception\n"));
    if (!m_fFileDownloadRunning) return false;

    m_fFileDownloadRunning = false;
    SetRecvTimeout(0);
    // adzm 2010-09
    SendKeepAlive(false, true);

    // sf@2004 - Delta transfer
    SetEndOfFile(m_hDestFile);

    // TODO : check dwNbReceivedPackets and dwTotalNbBytesWritten or test a checksum
    FlushFileBuffers(m_hDestFile);

    std::string realName = get_real_filename(m_szDestFileName);

    char szStatus[512 + 256];
    if (m_fFileDownloadError)
        sprintf(szStatus, " %s < %s > %s", sz_H19, realName.c_str(), sz_H20);
    else
        // sprintf(szStatus, " %s < %s > %s - %u bytes", sz_H17, m_szDestFileName,sz_H18, (m_dwTotalNbBytesWritten - m_dwTotalNbBytesNotReallyWritten));  // Testing
        sprintf(szStatus, " %s < %s > %s", sz_H17, realName.c_str(), sz_H18);

    SetStatus(szStatus);

    // Set the DestFile Time Stamp
    if (strlen(m_szIncomingFileTime)) {
        FILETIME DestFileTime;
        SYSTEMTIME FileTime;
        FileTime.wMonth = atoi(m_szIncomingFileTime);
        FileTime.wDay = atoi(m_szIncomingFileTime + 3);
        FileTime.wYear = atoi(m_szIncomingFileTime + 6);
        FileTime.wHour = atoi(m_szIncomingFileTime + 11);
        FileTime.wMinute = atoi(m_szIncomingFileTime + 14);
        FileTime.wMilliseconds = 0;
        FileTime.wSecond = 0;
        SystemTimeToFileTime(&FileTime, &DestFileTime);
        // ToDo: hook error
        SetFileTime(m_hDestFile, &DestFileTime, &DestFileTime, &DestFileTime);
    }

    CloseHandle(m_hDestFile);

    // sf@2004 - Delta Transfer - Now we can keep the existing file data :)
    if (m_fFileDownloadError && (UsingOldProtocol() || m_fUserAbortedFileTransfer)) DeleteFile(m_szDestFileName);

    // sf@2003 - Directory Transfer trick
    // If the file is an Ultra Directory Zip we unzip it here and we delete the
    // received file
    // Todo: make a better free space check (above) in this particular case. The free space must be at least
    // 3 times the size of the directory zip file (this zip file is ~50% of the real directory size) 

    // hide the stop button
    ShowWindow(GetDlgItem(hWnd, IDC_ABORT_B), SW_HIDE);
    bool bWasDir = UnzipPossibleDirectory(m_szDestFileName);
    ShowWindow(GetDlgItem(hWnd, IDC_ABORT_B), SW_SHOW);

    if (!m_fFileDownloadError && !bWasDir) {
        if (!::MoveFileEx(m_szDestFileName, realName.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            // failure. Updated status
            sprintf(szStatus, " %s < %s > %s", sz_H12, realName.c_str(), sz_H16);
            SetStatus(szStatus);
        }
    }

    // SetStatus(szStatus);
    UpdateWindow(hWnd);

    // Sound notif
    // MessageBeep(-1);

    // Request the next file in the list
    RequestNextFile();
    return true;
}
*/

//
// Set gauge value
//
void ConnectionConsole::SetGauge(__int64 dwCount)
{
    //	vnclog.Print(0, _T("SetGauge\n"));
    DWORD dwSmallerCount = (DWORD)(dwCount / m_nBlockSize);
    DWORD dwSmallerFileSize = (DWORD)(m_nnFileSize / m_nBlockSize);
    if (dwSmallerFileSize == 0) dwSmallerFileSize = 1;
    DWORD dwValue = (DWORD)((((__int64)(dwSmallerCount)* m_nBlockSize / dwSmallerFileSize)));
    if (dwValue != m_dwCurrentValue) {
        m_dwCurrentValue = dwValue;
    }

    DWORD dwPercent = (DWORD)(((__int64)(dwSmallerCount)* 100 / dwSmallerFileSize));
    if (dwPercent != m_dwCurrentPercent)
    {
        // adzm - Include the speed and kb total
        DWORD dwMsElapsed = GetTickCount() - m_dwStartTick;
        double dKbTotal = double(dwCount) / 1024;
        double dKbps = (dKbTotal / (double(dwMsElapsed) / 1000));
        printf("\r%s %d%% (%4.0f kb @ ~%4.0f kb/s)", m_szSrcFileNamee, dwPercent, dKbTotal, dKbps);
        fflush(stdout);
        m_dwCurrentPercent = dwPercent;
    }
}

// リモートにファイル存在するか
void ConnectionConsole::CheckRemoteFile(LPTSTR szPath) {

    // フォルダ名、ファイル名を抽出
    TCHAR szFolderPath[MAX_PATH] = { _T('\0') };
    strcpy(szFolderPath, szPath);
    m_szSrcFileNamee = strrchr(szPath, '\\');
    if (m_szSrcFileNamee) {
        m_szSrcFileNamee++;
        szFolderPath[m_szSrcFileNamee - szPath] = _T('\0');
    }

    // 指定フォルダのファイル一覧を受信しながら、
    // 指定ファイルが存在するかをチェック
    // →見つかった場合のみ、0(成功)をセット
    RequestRemoteDirectoryContent(szFolderPath);
}

//
// Request the contents of a remote directory
//
void ConnectionConsole::RequestRemoteDirectoryContent(LPTSTR szPath)
{
    //	vnclog.Print(0, _T("RequestRemoteDirectoryContent\n"));
    if (!m_fFTAllowed) {
        m_fFileCommandPending = false;
        return;
    }
    int len = _tcslen(szPath);
    if (len == 0) {
        m_fFileCommandPending = false;
        return;
    }
    len *= sizeof(TCHAR);

    rfbFileTransferMsg ft;
    ft.type = rfbFileTransfer;
    ft.contentType = rfbDirContentRequest;
    ft.contentParam = rfbRDirContent; // Directory content please
    ft.length = Swap32IfLE(len);
    //adzm 2010-09
    WriteExactQueue((char *)&ft, sz_rfbFileTransferMsg, rfbFileTransfer);
    WriteExact((char *)szPath, len);
    m_waitAnswer = true;

    return;
}

//
// Populate the remote machine listbox with files received from server
// ※consoleの場合、listboxないのですが、処理の始まりとして本関数を流用
//
void ConnectionConsole::PopulateRemoteListBox(int nLen) {
    //	vnclog.Print(0, _T("PopulateRemoteListBox\n"));
    // If the distant media is not browsable for some reason
    if (nLen == 0) {
        return;
    }

    // sf@2004 - Read the returned Directory full path
    if (nLen > 1 && !UsingOldProtocol()) {
        TCHAR szPath[MAX_PATH];
        if (nLen > sizeof(szPath)) return;
        ReadString((char *)szPath, nLen);
    }

    // The dir in the current packet
    memset(&m_fd, '\0', sizeof(WIN32_FIND_DATA));
    m_nFileCount = 0;
    m_fDirectoryReceptionRunning = true;
    m_waitAnswer = true;

}

void ConnectionConsole::ReceiveDirectoryItem(int nLen) {
    //	vnclog.Print(0, _T("ReceiveDirectoryItem\n"));
    if (!m_fDirectoryReceptionRunning) return;
    if (nLen > sizeof(m_szFileSpec)) return;

    // Read the File/Directory full info
    ReadString((char *)m_szFileSpec, nLen);
    memset(&m_fd, '\0', sizeof(WIN32_FIND_DATA));
    memcpy(&m_fd, m_szFileSpec, nLen);
    m_nFileCount++;
    if (m_cmd == CMD_CHECK && m_szSrcFileNamee) {
        if (//((m_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY) &&
            (0 == _tcsicmp(m_fd.cFileName, m_szSrcFileNamee))) {
            m_ret = 0;
        }
    }
    m_waitAnswer = true;
}

void ConnectionConsole::FinishDirectoryReception() {
    //	vnclog.Print(0, _T("FinishDirectoryReception\n"));
    if (!m_fDirectoryReceptionRunning) {
        m_waitAnswer = true;
        return;
    }
    m_fDirectoryReceptionRunning = false;
}

//////////////////////////////////////////////////////////////////////////////
// 通信系
//////////////////////////////////////////////////////////////////////////////
void ConnectionConsole::ReadExact(char *inbuf, int wanted) {
    if (wanted == 0) {
        return;
    }
    fis->readBytes(inbuf, wanted);
}

void ConnectionConsole::WriteExact(char *buf, int bytes){
    WriteTransformed(buf, bytes, false);
}
void ConnectionConsole::WriteExact(char *buf, int bytes, CARD8 msgType) {
    WriteTransformed(buf, bytes, msgType, false);
}
void ConnectionConsole::WriteTransformed(char *buf, int bytes, bool bQueue) {
    if (bytes == 0) return;

    char *pBuffer = buf;
    Write(pBuffer, bytes, bQueue);
}
void ConnectionConsole::WriteTransformed(char *buf, int bytes, CARD8 msgType, bool bQueue) {
    WriteTransformed(buf, bytes, bQueue);
}
void ConnectionConsole::WriteExactQueue(char *buf, int bytes) {
    WriteTransformed(buf, bytes, true);
}
void ConnectionConsole::WriteExactQueue(char *buf, int bytes, CARD8 msgType) {
    WriteTransformed(buf, bytes, msgType, true);
}
void ConnectionConsole::FlushWriteQueue(bool bTimeout, int timeout) {
    Write(NULL, 0, false, bTimeout, timeout);
}
void ConnectionConsole::Write(char *buf, int bytes, bool bQueue, bool bTimeout, int timeout)
{
    if (bytes == 0 && (bQueue || (!bQueue && m_nQueueBufferLength == 0))) return;

    // this will adjust buf and bytes to be < G_SENDBUFFER
    FlushOutstandingWriteQueue(buf, bytes, bTimeout, timeout);

    // append buf to any remaining data in the queue, since we know that m_nQueueBufferLength + bytes < G_SENDBUFFER
    if (bytes > 0) {
        memcpy(m_QueueBuffer + m_nQueueBufferLength, buf, bytes);
        m_nQueueBufferLength += bytes;
    }

    if (!bQueue) {
        DWORD i = 0;
        DWORD j = 0;
        while (i < m_nQueueBufferLength)
        {
            //adzm 2010-08-01
            m_LastSentTick = GetTickCount();
            if (bTimeout) {
                j = Send(m_QueueBuffer + i, m_nQueueBufferLength - i, timeout);
            }
            else {
                j = send(m_sock, m_QueueBuffer + i, m_nQueueBufferLength - i, 0);
            }
            if (j == SOCKET_ERROR || j == 0)
            {
                //m_running = false;
            }
            i += j;
            m_BytesSend += j;
        }

        m_nQueueBufferLength = 0;
    }
}
void ConnectionConsole::FlushOutstandingWriteQueue(char*& buf2, int& bytes2, bool bTimeout, int timeout)
{
    DWORD nNewSize = m_nQueueBufferLength + bytes2;

    while (nNewSize >= G_SENDBUFFER) {
        //adzm 2010-08-01
        m_LastSentTick = GetTickCount();

        int bufferFill = G_SENDBUFFER - m_nQueueBufferLength;

        // add anything from buf2 to the queued packet
        memcpy(m_QueueBuffer + m_nQueueBufferLength, buf2, bufferFill);

        // adjust buf2
        buf2 += bufferFill;
        bytes2 -= bufferFill;

        m_nQueueBufferLength = G_SENDBUFFER;

        int sent = 0;
        if (bTimeout) {
            sent = Send(m_QueueBuffer, G_SENDBUFFER, timeout);
        }
        else {
            sent = send(m_sock, m_QueueBuffer, G_SENDBUFFER, 0);
        }
        if (sent == SOCKET_ERROR || sent == 0)
        {

        }

        // adjust our stats
        m_BytesSend += sent;

        // adjust the current queue
        nNewSize -= sent;
        m_nQueueBufferLength -= sent;
        // if not everything, move to the beginning of the queue
        if ((G_SENDBUFFER - sent) != 0) {
            memcpy(m_QueueBuffer, m_QueueBuffer + sent, G_SENDBUFFER - sent);
        }
    }
}
int ConnectionConsole::Send(const char *buff, const unsigned int bufflen, int timeout) {
    struct fd_set write_fds;
    struct timeval tm;
    int count;
    int aa = 0;

    FD_ZERO(&write_fds);
    FD_SET((unsigned int)m_sock, &write_fds);
    tm.tv_sec = timeout;
    tm.tv_usec = 0;
    count = select(m_sock + 1, NULL, &write_fds, NULL, &tm);

    if (count == 0)
        return 0; //timeout
    if (count < 0 || count > 1)
        return -1;

    //adzm 2010-08-01
    m_LastSentTick = GetTickCount();

    if (FD_ISSET((unsigned int)m_sock, &write_fds)) aa = send(m_sock, buff, bufflen, 0);
    return aa;
}

// Makes sure netbuf is at least as big as the specified size.
// Note that netbuf itself may change as a result of this call.
// Throws an exception on failure.
void ConnectionConsole::CheckBufferSize(int bufsize) {
    // sf@2009 - Sanity check
    if (bufsize < 0 || bufsize > 104857600) // 100 MBytes max
    {
        throw ErrorExceptionConsole("Insufficient memory to allocate network buffer.");
    }

    if (m_netbufsize > bufsize) return;

    char *newbuf = new char[bufsize + 256];
    if (newbuf == NULL) {
        throw ErrorExceptionConsole("Insufficient memory to allocate network buffer.");
    }

    // Only if we're successful...

    if (m_netbuf != NULL)
        delete[] m_netbuf;
    m_netbuf = newbuf;
    m_netbufsize = bufsize + 256;
    vnclog.Print(4, _T("bufsize expanded to %d\n"), m_netbufsize);
}
void ConnectionConsole::ReadString(char *buf, int length) {
    if (length > 0)
        ReadExact(buf, length);
    buf[length] = '\0';
    vnclog.Print(10, _T("Read a %d-byte string\n"), length);
}
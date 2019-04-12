#ifndef __MSGMANAGER__
#define __MSGMANAGER__
#include "stdafx.h"

////////////////////////////////
//
#define GETMSG(no) MsgMgr::GetMsg(no)

////////////////////////////////
// メッセージNo定義
#define COMMON_ERROR            0x00000000
#define COMMON_SCRIPT_OPEN      COMMON_ERROR + 0x01
#define COMMON_SCRIPT_CHECK     COMMON_ERROR + 0x02
#define COMMON_SCRIPT_NOTNOW    COMMON_ERROR + 0x03

#define INIT_ERROR              0x00000100
#define INIT_WSASTARTUP         INIT_ERROR + 0x01
#define INIT_CREATE_SOCKET_ERR  INIT_ERROR + 0x02
#define INIT_GETHOST_ERR        INIT_ERROR + 0x03
#define INIT_CONNECT_ERR        INIT_ERROR + 0x04
#define INIT_SETSOCKET_ERR      INIT_ERROR + 0x05
#define INIT_SERVER_OLD         INIT_ERROR + 0x06

// メッセージ定義
class MsgMgr {
public:
    MsgMgr() {}
    virtual ~MsgMgr(){}

    static LPCTSTR GetMsg(DWORD msgno) {
        switch (msgno) {
        case COMMON_SCRIPT_OPEN: return _T("スクリプトファイルOpen失敗 %ld");
        case COMMON_SCRIPT_CHECK: return _T("スクリプトファイル不正 %ld");
        case COMMON_SCRIPT_NOTNOW: return _T("対応していないコマンド %s");
        case INIT_WSASTARTUP: return _T("WSAStartup 失敗 %ld");
        case INIT_CREATE_SOCKET_ERR: return _T("socket 失敗. エラー=%ld");
        case INIT_GETHOST_ERR: return _T("gethostbyname 失敗. エラー=%ld");
        case INIT_CONNECT_ERR: return _T("connect 失敗. エラー=%ld");
        case INIT_SETSOCKET_ERR: return _T("setsockopt 失敗. エラー=%ld");
        case INIT_SERVER_OLD: return _T("サーバがファイル配信をサポートしない");
        default:return _T("未定義");
        }
    }
};
#endif // __MSGMANAGER__
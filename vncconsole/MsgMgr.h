#ifndef __MSGMANAGER__
#define __MSGMANAGER__
#include "stdafx.h"

////////////////////////////////
//
#define GETMSG(no) MsgMgr::GetMsg(no)

////////////////////////////////
// ���b�Z�[�WNo��`
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

// ���b�Z�[�W��`
class MsgMgr {
public:
    MsgMgr() {}
    virtual ~MsgMgr(){}

    static LPCTSTR GetMsg(DWORD msgno) {
        switch (msgno) {
        case COMMON_SCRIPT_OPEN: return _T("�X�N���v�g�t�@�C��Open���s %ld");
        case COMMON_SCRIPT_CHECK: return _T("�X�N���v�g�t�@�C���s�� %ld");
        case COMMON_SCRIPT_NOTNOW: return _T("�Ή����Ă��Ȃ��R�}���h %s");
        case INIT_WSASTARTUP: return _T("WSAStartup ���s %ld");
        case INIT_CREATE_SOCKET_ERR: return _T("socket ���s. �G���[=%ld");
        case INIT_GETHOST_ERR: return _T("gethostbyname ���s. �G���[=%ld");
        case INIT_CONNECT_ERR: return _T("connect ���s. �G���[=%ld");
        case INIT_SETSOCKET_ERR: return _T("setsockopt ���s. �G���[=%ld");
        case INIT_SERVER_OLD: return _T("�T�[�o���t�@�C���z�M���T�|�[�g���Ȃ�");
        default:return _T("����`");
        }
    }
};
#endif // __MSGMANAGER__
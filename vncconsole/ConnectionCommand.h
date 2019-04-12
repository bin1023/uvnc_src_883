#ifndef __CONNECTION_COMMAND__
#define __CONNECTION_COMMAND__
#include "stdafx.h"
#include <list>

//////////////////////////////////////////////////////////////////////////////
// �R�}���h��`�ꗗ
// �o�^�֌W
#define CMD_OPEN        1   // �ڑ�
#define CMD_CLOSE       2   // �ؒf
#define CMD_REQPERM     3   // 
#define CMD_EXIT        4   // �I��
// �����[�g����
#define CMD_PWD         21  // 
#define CMD_CD          22  // �����[�gCD
#define CMD_LS          23  // �����[�gLS
#define CMD_GET         24  // �����[�g����t�@�C����M
#define CMD_PUT         25  // �����[�g�փt�@�C�����M
#define CMD_MKDIR       26  // �����[�g�Ƀt�H���_���쐬
#define CMD_RM          27  // �����[�g�̃t�@�C�����폜
#define CMD_RENAME      28  // �����[�g�̃t�@�C�������l�[��
#define CMD_CHECK       29  // �����[�g�̃t�@�C�������݂��邩�`�F�b�N
// ���[�J������
#define CMD_LCD         40  // ���[�J��CD

// �R�}���h��`�i���ʁj
typedef struct tagConsoleCmd {
    DWORD no;  // �R�}���h
    TCHAR data; // �R�}���h�f�[�^
}ConsoleCmd, *PConsoleCmd;
typedef std::list<ConsoleCmd*> CmdList;

// �ڑ��p
#define HOST_MAX_LEN 255
#define PASS_MAX_LEN 255
#define DEFAULT_PORT 5900
typedef struct tagConsoleCmdOpenData {
    TCHAR host[HOST_MAX_LEN+1];
    TCHAR pass[PASS_MAX_LEN+1];
    UINT  port;
}CCOpenData, *PCCOpenData;

// �����[�g�փt�@�C�����M
// CMD_PUT
#define PATH_MAX_LEN 255
typedef struct tagConsoleCmdPutData {
    TCHAR from[PATH_MAX_LEN+1]; // ���[�J���t���p�X
    TCHAR to[PATH_MAX_LEN+1];   // �����[�g�t���p�X
}CCPutData, *PCCPutData;

// �����[�g�t�@�C���̑��݃`�F�b�N
// CMD_CHECK
typedef struct tagConsoleCmdCheckData {
    TCHAR path[PATH_MAX_LEN+1];   // �����[�g�t���p�X
}CCCheckData, *PCCCheckData;

#endif //__CONNECTION_COMMAND__

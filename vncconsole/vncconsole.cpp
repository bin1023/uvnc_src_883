// vncconsole.cpp : �R���\�[�� �A�v���P�[�V�����̃G���g�� �|�C���g���`���܂��B
//

#include "stdafx.h"
#include "LogConsole.h"
#include "ExceptionConsole.h"
#include "ConnectionConsole.h"
#include "OptionsConsole.h"
#include "ConnectionCommand.h"

///
LogConsole vnclog;
OptionsConsole vncopt;
///
static BOOL GetNextCommand(PConsoleCmd &command);

// TODO:UltraVNCViewer�̂܂܁ATCHAR/char���p�B
int _tmain(int argc, TCHAR* argv[]) {
    // �����`�F�b�N
    if (argc > 1) {
        vncopt.SetFromCommandLine(argc - 1, argv + 1);
    }
    vnclog.SetLevel(vncopt.m_logLevel);
    vnclog.SetMode(LogConsole::ToConsole);

    // ��host,password�͈�������擾���邽�߁AStartup�͂����Ŏ��{
    // ��TODO:host/password��stdin���͂ł���悤�Ή�
    ConnectionConsole console;
    if (vncopt.m_Open == NULL) {
        vnclog.Print(0, _T("Host,Password���w��"));
        return -1;
    }
    if (console.Startup(vncopt.m_Open)) {
        // ���O��Startup�ɂďo�͍ς�
        return -1;
    }

    // �`�F�b�N�̏ꍇ�́A�`�F�b�N��Exit
    int ret = 0;
    int chkng = 1;
    PConsoleCmd cmd = NULL;
    if (vncopt.m_mode == MD_CHECK) {
        // �`�F�b�N���[�h
        int chkret = 0;
        while (GetNextCommand(cmd)) {
            if (cmd->no != CMD_CHECK) continue;
            // �R�}���h���{
            chkng *= 2; // �s���̂Q��
            chkret = console.ExecCmd(cmd->no, &cmd->data);
            if (0 != chkret) {
                ret += chkng; // ���ڂ̃R�}���h�����s��������Ԃ��i�����͑��s�j
            }
        }
    } else {
        // �ʏ탂�[�h
        while (GetNextCommand(cmd)) {
            if (cmd->no == CMD_CHECK) continue;
            // �R�}���h���{
            ret = console.ExecCmd(cmd->no, &cmd->data);
            if (/* StopWhenFail && */ 0 != ret) {
                break;
            }
        }
    }

    console.Cleanup();
    return ret;
}

BOOL GetNextCommand(PConsoleCmd &cmd) {
    cmd = vncopt.GetCommand();
    return cmd != NULL;
}

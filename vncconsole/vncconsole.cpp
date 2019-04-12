// vncconsole.cpp : コンソール アプリケーションのエントリ ポイントを定義します。
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

// TODO:UltraVNCViewerのまま、TCHAR/char併用。
int _tmain(int argc, TCHAR* argv[]) {
    // 引数チェック
    if (argc > 1) {
        vncopt.SetFromCommandLine(argc - 1, argv + 1);
    }
    vnclog.SetLevel(vncopt.m_logLevel);
    vnclog.SetMode(LogConsole::ToConsole);

    // ※host,passwordは引数から取得するため、Startupはここで実施
    // →TODO:host/passwordをstdin入力できるよう対応
    ConnectionConsole console;
    if (vncopt.m_Open == NULL) {
        vnclog.Print(0, _T("Host,Password未指定"));
        return -1;
    }
    if (console.Startup(vncopt.m_Open)) {
        // ログはStartupにて出力済み
        return -1;
    }

    // チェックの場合は、チェック＆Exit
    int ret = 0;
    PConsoleCmd cmd = NULL;
    while (GetNextCommand(cmd)) {
        if (cmd->no == CMD_CHECK) {
            if (vncopt.m_mode != MD_CHECK) continue;
        } else {
            if (vncopt.m_mode == MD_CHECK) continue;
        }

        // コマンド実施
        ret = console.ExecCmd(cmd->no, &cmd->data);
        if (/* StopWhenFail && */ 0 != ret) {
            break;
        }
    }

    console.Cleanup();
    return ret;
}

BOOL GetNextCommand(PConsoleCmd &cmd) {
    cmd = vncopt.GetCommand();
    return cmd != NULL;
}

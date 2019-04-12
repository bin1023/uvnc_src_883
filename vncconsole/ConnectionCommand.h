#ifndef __CONNECTION_COMMAND__
#define __CONNECTION_COMMAND__
#include "stdafx.h"
#include <list>

//////////////////////////////////////////////////////////////////////////////
// コマンド定義一覧
// 登録関係
#define CMD_OPEN        1   // 接続
#define CMD_CLOSE       2   // 切断
#define CMD_REQPERM     3   // 
#define CMD_EXIT        4   // 終了
// リモート操作
#define CMD_PWD         21  // 
#define CMD_CD          22  // リモートCD
#define CMD_LS          23  // リモートLS
#define CMD_GET         24  // リモートからファイル受信
#define CMD_PUT         25  // リモートへファイル送信
#define CMD_MKDIR       26  // リモートにフォルダを作成
#define CMD_RM          27  // リモートのファイルを削除
#define CMD_RENAME      28  // リモートのファイルをリネーム
#define CMD_CHECK       29  // リモートのファイルが存在するかチェック
// ローカル操作
#define CMD_LCD         40  // ローカルCD

// コマンド定義（共通）
typedef struct tagConsoleCmd {
    DWORD no;  // コマンド
    TCHAR data; // コマンドデータ
}ConsoleCmd, *PConsoleCmd;
typedef std::list<ConsoleCmd*> CmdList;

// 接続用
#define HOST_MAX_LEN 255
#define PASS_MAX_LEN 255
#define DEFAULT_PORT 5900
typedef struct tagConsoleCmdOpenData {
    TCHAR host[HOST_MAX_LEN+1];
    TCHAR pass[PASS_MAX_LEN+1];
    UINT  port;
}CCOpenData, *PCCOpenData;

// リモートへファイル送信
// CMD_PUT
#define PATH_MAX_LEN 255
typedef struct tagConsoleCmdPutData {
    TCHAR from[PATH_MAX_LEN+1]; // ローカルフルパス
    TCHAR to[PATH_MAX_LEN+1];   // リモートフルパス
}CCPutData, *PCCPutData;

// リモートファイルの存在チェック
// CMD_CHECK
typedef struct tagConsoleCmdCheckData {
    TCHAR path[PATH_MAX_LEN+1];   // リモートフルパス
}CCCheckData, *PCCCheckData;

#endif //__CONNECTION_COMMAND__

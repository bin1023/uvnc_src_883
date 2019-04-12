#ifndef __OPTIONS_CONSOLE__
#define __OPTIONS_CONSOLE__

#include "ConnectionCommand.h"

typedef enum {
    MD_CHECK,
    MD_NORMAL,
}ConsoleMode;
// vncconsole 
//  [/pass clearpassword]           パスワード
//  [/host aaa.bbb.ccc.ddd]         ホストIP
//  [/port portno]                  ポート
//  [/script \path\to\script.txt]   スクリプト
//  [/loglevel level]
class OptionsConsole
{
public:
    OptionsConsole();
    virtual ~OptionsConsole();
    UINT    m_logLevel;
    ConsoleMode m_mode;
    void SetFromCommandLine(LPTSTR szCmdLine);
    void SetFromCommandLine(int argc, TCHAR* args[]);
    PConsoleCmd GetCommand();

    // とりあえず
    PConsoleCmd m_Open;
    PConsoleCmd m_Check; // リモートのファイル存在をチェック

private:
    void LoadScriptFile(LPCTSTR filename);
    int CheckOneLine(PTCHAR line, PTCHAR sep[], int cnt);
    int CreateCommand(PTCHAR sep[], int cnt);
    CmdList m_cmdList;
    CmdList::iterator m_cmdIt;
};

#endif // __OPTIONS_CONSOLE__
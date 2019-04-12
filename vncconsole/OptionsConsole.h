#ifndef __OPTIONS_CONSOLE__
#define __OPTIONS_CONSOLE__

#include "ConnectionCommand.h"

typedef enum {
    MD_CHECK,
    MD_NORMAL,
}ConsoleMode;
// vncconsole 
//  [/pass clearpassword]           �p�X���[�h
//  [/host aaa.bbb.ccc.ddd]         �z�X�gIP
//  [/port portno]                  �|�[�g
//  [/script \path\to\script.txt]   �X�N���v�g
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

    // �Ƃ肠����
    PConsoleCmd m_Open;
    PConsoleCmd m_Check; // �����[�g�̃t�@�C�����݂��`�F�b�N

private:
    void LoadScriptFile(LPCTSTR filename);
    int CheckOneLine(PTCHAR line, PTCHAR sep[], int cnt);
    int CreateCommand(PTCHAR sep[], int cnt);
    CmdList m_cmdList;
    CmdList::iterator m_cmdIt;
};

#endif // __OPTIONS_CONSOLE__
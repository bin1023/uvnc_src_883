#include "stdafx.h"
#include "OptionsConsole.h"
#include "LogConsole.h"
#include "MsgMgr.h"

extern LogConsole vnclog;
OptionsConsole::OptionsConsole() {
    m_Open = NULL;
    m_Check = NULL;
    m_logLevel = 0;
    m_mode = MD_NORMAL; // �f�t�H���g�͎��{���[�h
}

OptionsConsole::~OptionsConsole() {
    for (m_cmdIt = m_cmdList.begin(); m_cmdIt != m_cmdList.end(); m_cmdIt++) {
        if (NULL != *m_cmdIt) {
            free (*m_cmdIt);
        }
    }
    m_cmdList.clear();

    if (m_Open) free(m_Open);
    if (m_Check) free(m_Check); 
}

inline bool SwitchMatch(LPCTSTR arg, LPCTSTR swtch) {
    return (arg[0] == '-' || arg[0] == '/') && (_tcsicmp(&arg[1], swtch) == 0);
}

void OptionsConsole::SetFromCommandLine(LPTSTR szCmdLine) {
    // We assume no quoting here.
    // Copy the command line - we don't know what might happen to the original
    int cmdlinelen = _tcslen(szCmdLine);
    if (cmdlinelen == 0) return;

    TCHAR *cmd = new TCHAR[cmdlinelen + 1];
    _tcscpy(cmd, szCmdLine);

    // Count the number of spaces
    // This may be more than the number of arguments, but that doesn't matter.
    int nspaces = 0;
    TCHAR *p = cmd;
    TCHAR *pos = cmd;
    while ((pos = _tcschr(p, ' ')) != NULL) {
        nspaces++;
        p = pos + 1;
    }

    // Create the array to hold pointers to each bit of string
    TCHAR **args = new LPTSTR[nspaces + 1];

    // replace spaces with nulls and
    // create an array of TCHAR*'s which points to start of each bit.
    pos = cmd;
    int i = 0;
    args[i] = cmd;
    bool inquote = false;
    for (pos = cmd; *pos != 0; pos++) {
        // Arguments are normally separated by spaces, unless there's quoting
        if ((*pos == ' ') && !inquote) {
            *pos = '\0';
            p = pos + 1;
            args[++i] = p;
        }
        if (*pos == '"') {
            if (!inquote) {      // Are we starting a quoted argument?
                args[i] = ++pos; // It starts just after the quote
            }
            else {
                *pos = '\0';     // Finish a quoted argument?
            }
            inquote = !inquote;
        }
    }
    i++;
    SetFromCommandLine(i, args);
    delete[] cmd;
    delete[] args;
}

const size_t size = sizeof(ConsoleCmd) + sizeof(CCOpenData);
inline PConsoleCmd MallocOpenCmd() {
    PConsoleCmd cmd = (PConsoleCmd)malloc(size);
    ZeroMemory(cmd, size);
    cmd->no = CMD_OPEN;
    (PCCOpenData(&cmd->data))->port = DEFAULT_PORT;
    return cmd;
}

void OptionsConsole::SetFromCommandLine(int argc, TCHAR* args[]) {
    for (int j = 0; j < argc; j++) {
        if (SwitchMatch(args[j], _T("pass"))) {
            if (++j == argc) continue;  // �p�X���[�h������
            if (_tcslen(args[j]) > PASS_MAX_LEN) continue; // ���߂�
            if (!m_Open) { m_Open = MallocOpenCmd(); }
            if (m_Open) {
                _tcscpy((PCCOpenData(&m_Open->data))->pass, args[j]);
            }
        } else if (SwitchMatch(args[j], _T("host"))) {
            if (++j == argc) continue;  // �z�X�g������
            if (_tcslen(args[j]) > HOST_MAX_LEN) continue; // ���߂�
            if (!m_Open) { m_Open = MallocOpenCmd(); }
            if (m_Open) {
                _tcscpy((PCCOpenData(&m_Open->data))->host, args[j]);
            }
        } else if (SwitchMatch(args[j], _T("port"))) {
            if (++j == argc) continue;  // �|�[�g������
            if (!m_Open) { m_Open = MallocOpenCmd(); }
            if (m_Open) {
                if (_stscanf(args[j], _T("%d"), 
                    &(PCCOpenData(&m_Open->data))->port) != 1) {
                    (PCCOpenData(&m_Open->data))->port = DEFAULT_PORT;
                    continue;
                }
            }
        } else if (SwitchMatch(args[j], _T("loglevel"))) {
            if (++j == argc) continue;
            if (_stscanf(args[j], _T("%d"), &m_logLevel) != 1) continue;
        } else if (SwitchMatch(args[j], _T("script"))) {
            if (++j == argc) continue;
            LoadScriptFile(args[j]);
        } else if (SwitchMatch(args[j], _T("check"))) {
            // Check���[�h�{�X�N���v�g�t�@�C���őΉ�
            m_mode = MD_CHECK;
        //} else if (SwitchMatch(args[j], _T("check"))) {
        //    if (++j == argc) continue;  // �z�X�g������
        //    if (_tcslen(args[j]) > PATH_MAX_LEN) continue; // ���߂�
        //    if (!m_Check) { 
        //        m_Check = (PConsoleCmd)malloc(sizeof(ConsoleCmd) + sizeof(CCCheckData));
        //        m_Check->no = CMD_CHECK;
        //        _tcscpy((PCCCheckData(&m_Check->data))->path, args[j]);
        //    }
        } else {
            // ���̑����Ή��̃p�����[�^
        }
    }
}

// (space)̨����A space ̨����B space �c ̨����X(space)(\r\n)
// �߂�l:̨���ސ� (<0�͎��s)
int OptionsConsole::CheckOneLine(PTCHAR line, PTCHAR sep[], int cnt) {
    // �ꉞ�A�������`�F�b�N
    if (NULL == line || NULL == sep || cnt <= 0) {
        return 0;
    }

    PTCHAR begin = NULL, end = line;
    bool gotit = false;
    bool inquote = false;
    bool done = false;
    int idx = 0; // �t�B�[���h�̃C���f�N�X=�߂�l
    while (!done) {
        switch (*end) {
        case _T(' '): 
            if (begin == NULL || inquote) { // �������J�n�A�܂���""��
                end++;
            } else {
                gotit = true;
            }
            break;
        case _T('"'):
            if (inquote) { 
                gotit = true;
            } else {
                end++; // "���X�L�b�v
                if (begin == NULL) { begin = end; } // �����J�n
            }
            inquote = !inquote;
            break;
        case _T('\r'):
        case _T('\n'):
        case _T('\0'):
            done = true; // �����I��
            if (inquote) {
                return -1; // ��`�s��
            } else {
                gotit = begin != NULL;
            }
            break;
        default:
            if (begin == NULL) { begin = end; } // �����J�n
            end++;
            break;
        }

        if (gotit) {
            if (idx == cnt) {
                return -2; // �I�[�o�[
            }
            *end++ = _T('\0');
            sep[idx++] = begin;
            begin = NULL; // ����̨���ފJ�n��҂�
            gotit = false;
        }
    }

    return idx;
}

void OptionsConsole::LoadScriptFile(LPCTSTR filename){
    if (NULL == filename) return;

    FILE *script = NULL;
    errno_t err = _tfopen_s(&script, filename, _T("r"));
    if (0 != err || NULL == script) {
        vnclog.Print(0, GETMSG(COMMON_SCRIPT_OPEN), err);
        return;
    }

    TCHAR buf[1024];
    const int COL_MAX = 4; // 1�s��MAX3�f�[�^(cmd,����1,2,3)
    while (_fgetts(buf, _countof(buf), script)) {
        // �R�}���h �����i�X�y�[�X�ŋ�؂�j
        // �����ɃX�y�[�X������ꍇ�A�_�u���N�H�[�e�[�V�����ň͂�
        PTCHAR cols[COL_MAX] = { 0 };
        int cnt = CheckOneLine(buf, cols, COL_MAX);
        if ( cnt > 0 ) {
            CreateCommand(cols, cnt);
        } else {
            // ��͎��s
            vnclog.Print(0, GETMSG(COMMON_SCRIPT_CHECK), cnt);
            break;
        }
    }
    // fgets�ُ͈�܂���eof���I���B
    fclose(script);
    m_cmdIt = m_cmdList.begin();
}

// sep[0]:command
int OptionsConsole::CreateCommand(PTCHAR sep[], int cnt) {
    if (NULL == sep || cnt <= 0) {
        return 0;
    }

    PConsoleCmd cmd = NULL;
    if (_tcsicmp(sep[0], _T("put")) == 0) {
        if (cnt != 3) {
            return 0; // �����ȃR�}���h�𖳎��H
        }
        cmd = (PConsoleCmd)malloc(sizeof(ConsoleCmd) + sizeof(CCPutData));
        cmd->no = CMD_PUT;
        _tcscpy_s((PCCPutData(&cmd->data))->from, sep[1]);
        _tcscpy_s((PCCPutData(&cmd->data))->to, sep[2]);
    } else if (_tcsicmp(sep[0], _T("bye")) == 0 || _tcsicmp(sep[0], _T("exit")) == 0) {
        cmd = (PConsoleCmd)malloc(sizeof(ConsoleCmd));
        cmd->no = CMD_EXIT;
    } else if (_tcsicmp(sep[0], _T("check")) == 0) {
        cmd = (PConsoleCmd)malloc(sizeof(ConsoleCmd) + sizeof(CCCheckData));
        cmd->no = CMD_CHECK;
        _tcscpy_s((PCCCheckData(&cmd->data))->path, sep[1]);
    } else {
        // ���Ή��̃R�}���h
        vnclog.Print(0, GETMSG(COMMON_SCRIPT_NOTNOW), sep[0]);
        return 0;
    }

    m_cmdList.push_back(cmd);
    return 0;
}

PConsoleCmd OptionsConsole::GetCommand() {
    if (m_cmdIt == m_cmdList.end()) {
        return NULL;
    } else {
        return *m_cmdIt++;
    }
}
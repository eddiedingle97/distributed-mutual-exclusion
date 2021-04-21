#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

const short SERVERPORTS[] = {55793, 55792, 55794};
enum COMMANDS {READ, WRITE, ENQUIRE, DISCONNECT};
enum RESPONSES {ACCEPT, DENY, CANCEL};
char *filedirs[] = {"files1", "files2", "files3"};

struct fileinfo
{
    char *filename;
    int clientid;
    char operation;
};

#endif

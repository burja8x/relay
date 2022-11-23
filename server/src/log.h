#include <stdbool.h>
#include <stdint.h>
//
#define TRACE 0
#define DEBUG 1
#define INFO 2
#define WARNING 3
#define ERROR 4
#define FATAL 5


void CloseLogFile(void);
void OpenLogFile(bool mole);
char * GenerateLogFile();
void AppendToLogFile(unsigned int who, unsigned int level, char* message, unsigned int len);
void Log(unsigned int level, const char *fmt, ...);
void SetDisplayLogs(unsigned int i);
char* GetLogLoc(void);


int anyNewLog(void);
char* getPtrToLastLogText(void);
void resetLestLogText(void);

char* hex_text(uint8_t* buf, int len);

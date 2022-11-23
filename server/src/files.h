//
#include <stdbool.h>

long getContentOfLastFile();
void appendToLastFile(char *name, char *data, int len, bool closeFile);
void closeLastFile(void);
void createFile(char *name, char* dir);
void appendToFile(char *name, char *dir, char *data, int len);
long readFile(char *name, char* dir, char *buf);
void deleteFile(char *name);
void setWorkingPath(void);
char * getWorkingPath();
void createDirIfNotExist(char * dirName);
bool fileExist(char * fileName, char * dir);
void SaveTtyOutput(char* message);
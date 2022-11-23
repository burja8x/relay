#include <errno.h>
#include <stdio.h>
#include "log.h"
// #include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "files.h"
#include "ws.h"
#include "pipe.h"
#include <stdint.h>
#include <sys/time.h>

static unsigned int display_logs_from = 0;
static const char *LEVELS[] = {"TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
static const char *FROM_WHO[] = {"PM MOLE", "PM C MOLE", "MOLE", "PROXY", "PM C PROXY", "PM PROXY"}; //0 - 5 
static bool mole = false;

static FILE *fd = NULL;
// time_t begin;
// clock_t begin;
struct timeval start, end;
static char *logFileLocation = NULL;

static long EOFLog = 100000;
static char* bufRead = NULL;
static char *line = NULL;
static long logSize = 0;
static long readFromHere = 0;

void AppendToLogFile(unsigned int who, unsigned int level, char* message, unsigned int len){
    if(who >5 || level >5){
        printf("ERROR LOG who:%u level:%u\n", who, level);
    }
    if(len > 13 && memcmp("[[34m#[0m] ", message, 13) == 0){ // msg. from proxmark
        message = message + 13;
        if(who == 1){
            who--;
        }else if(who == 4){
            who++;
        }else{
            printf("ERROR LOG who:%u level:%u ....\n", who, level);
        }
    }
    // clock_t end = clock();
    // time_t end = time(NULL);
    errno = 0;
    if(line == NULL){
        line = (char *)calloc(2048, sizeof(char));
    }
    memset(line, 0, 2048);
    if(len > 2040){
        len = 1500;
    }
    // char *line = (char *)calloc(500+len, sizeof(char));
    // double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    gettimeofday(&end, NULL);
    long seconds = (end.tv_sec - start.tv_sec);
    long micros = ((seconds * 1000000) + end.tv_usec) - (start.tv_usec);
    double time_spent = micros / (double)1000000;
    unsigned int length = sprintf(line, "%-8.3f: %-11s %-8s %.*s\n", time_spent, FROM_WHO[who], LEVELS[level], len, message);

    if (level >= display_logs_from){
        if(level >= 4){
            fprintf(stderr, "%s", line);
            fflush(stderr);
        }else{
            printf("%s", line);
            fflush(stdout);
        }
    }

    int res = fprintf(fd, "%s", line);
    // int res = fprintf(fd, "%-6u: %-10s %-9s %s\n", running_s, FROM_WHO[who], LEVELS[level], message);

    if (res <= 0) {
        printf("Unable to write to log file!!! %s\n", strerror(errno));
        // free(line);
        return;
    }
    fflush(fd);

    // Only PROXY
    if( ! mole){
        if(bufRead == NULL){ bufRead = calloc(EOFLog, sizeof(char)); logSize = 0;}
        if(logSize+length >= EOFLog){
            printf("----  %lu", logSize); fflush(stdout);
            EOFLog = EOFLog * 2;
            // char* bufRead_new = calloc(EOFLog, sizeof(char));
            char *bufRead_new = realloc(bufRead, EOFLog);
            if (bufRead_new) {
                bufRead = bufRead_new;
            }else{
                printf("error realloc() errno: %s\n", strerror(errno)); fflush(stdout);
                // free(line);
                return;
            }
            // memcpy(bufRead_new, bufRead, logSize); // copy old logs
            // free(bufRead);
            // bufRead = bufRead_new;
            printf("  ----\n"); fflush(stdout);
        }
        memcpy(bufRead+logSize, line, length);
        
        logSize += length;
    }    
    // free(line);
}

void SetDisplayLogs(unsigned int i){
    display_logs_from = i;
}

void Log(unsigned int level, const char *fmt, ...){
    char buffer[1024] = {0};
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if(Is_WS_Null() && mole){
        strcat(buffer, "  >    ERROR: NOT connected to proxy !");
    }
    AppendToLogFile(mole?2:3, level, buffer, len);
    if(mole && ! Is_WS_Null()){
        uint8_t * data = (uint8_t*)calloc(len+5, sizeof(uint8_t));
        data[0] = ((len+2) >> 0) & 0xFF;
        data[1] = ((len+2) >> 8) & 0xFF;
        data[2] = P_LOG;
        data[3] = 2; // MOLE
        data[4] = (uint8_t)level;
        memcpy(data+5, buffer, len);
        Send_WS(data, 5 + len);
        free(data);
    }
}
void resetLestLogText(void){
    readFromHere = 0;
}
int anyNewLog(void){
    return logSize - readFromHere;
}
char* getPtrToLastLogText(void){
    if(bufRead == NULL){ bufRead = calloc(EOFLog, sizeof(char)); logSize = 0;}
    long tmp_readFromHere = readFromHere;
    readFromHere = logSize;
    return bufRead + tmp_readFromHere;
}

char* hex_text(uint8_t* buf, int len){
    static char hex[2048] = {0};
    memset(hex, 0, 2048);
    char *tmp = (char *)hex;

    for (int i = 0; i*3 < 2048 && i < len; ++i, tmp += 3) {
        sprintf(tmp, "%02X ", (unsigned int) buf[i]);
    }

    *tmp = '\0';
    return hex;
}

char * GenerateLogFile(){
    char * text = (char *)calloc(100, sizeof(char));
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(text, 99, "%m%d_%H-%M-%S.log", t);
    printf("Log file with name: %s\n", text);
    return text;
}

void OpenLogFile(bool is_mole){
    mole = is_mole;
    char * filename = GenerateLogFile();
    char * path = (char *)calloc(300, sizeof(char));
    strcat(path, getWorkingPath());
    strcat(path, "/log/");
    if(mole){
        strcat(path, "mole_");
    }
    strcat(path, filename);

    createDirIfNotExist("log");

    // begin = time(NULL);
    // begin = clock();
    gettimeofday(&start, NULL);

    logFileLocation = path;
    printf("log file will be ->%s\n", path);

    //createFile(filename, "log");

    fd = fopen(path, "a+");
    if(fd == NULL){
        printf("ERROR when opening log file!!! filename:%s  errno: %s\n", filename, strerror(errno));
    }
    // free(path);
    free(filename);
}

void CloseLogFile(void){
    fclose(fd);
}

char* GetLogLoc(void){
    return logFileLocation;
}
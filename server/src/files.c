
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "log.h"

static FILE *lastFile = NULL;
static FILE *fdt = NULL;
static char *lastFileName = NULL;
static char *lastFileBuffer;
static bool lastFileUpdated = true;
static char *workingPath = NULL;


long getContentOfLastFile(){
    if(lastFile != NULL){
        fclose(lastFile);
    }
    errno = 0;
    lastFile = fopen(lastFileName , "rb");
    if( ! lastFile ){
        Log(ERROR, "getContentOfLastFile()  %s", strerror(errno));
        //perror(lastFileName);
        return -1;
    }

    fseek(lastFile , 0L , SEEK_END);
    long lSize = ftell(lastFile);
    rewind(lastFile);

    lastFileBuffer = calloc(lSize+1, sizeof(char));
    // lastFileBuffer = calloc(1, lSize+1);
    if( !lastFileBuffer ){
        fclose(lastFile);
        Log(ERROR, "memory alloc fails");
        return -2;
    }

    errno = 0;
    if(1 != fread(lastFileBuffer , lSize, 1 , lastFile)){
        fclose(lastFile);
        free(lastFileBuffer);
        lastFileBuffer = NULL;
        Log(ERROR, "fread() fails %s", strerror(errno));
        return -3;
    }

    fclose(lastFile);
    return lSize;
}

void appendToLastFile(char *name, char *data, int len, bool closeFile){
    if(lastFile == NULL){
        lastFile = fopen(name, "ab");
        if( !lastFile ){
            //perror(name);
            Log(ERROR, "appendToLastFile() fails");
            return;
        }
    }
    fwrite(data, len, 1, lastFile);
    fflush(lastFile);
    if(closeFile){
        fclose(lastFile);
        lastFile = NULL;
    }
    lastFileUpdated = true;
}

void setWorkingPath(void){
    if(workingPath != NULL) return;

    workingPath = (char *)calloc(256, sizeof(char));
    errno = 0;

    if (getcwd(workingPath, 256) == NULL) {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }
    printf("Current working directory: %s\n", workingPath);
}
char * getWorkingPath(){
    setWorkingPath();
    return workingPath;
}

void appendToFile(char *name, char *dir, char *data, int len){
    char *filePath;// = (char *)calloc(512, sizeof(char));
    asprintf(&filePath, "%s/%s/%s", workingPath, dir, name);

    FILE *f = fopen(filePath, "ab");
    if( !f ){
        perror(filePath);
        free(filePath);
        return;
    }
    
    fwrite(data, len, 1, f);
    fflush(f);

    fclose(f);
    free(filePath);
}

void closeLastFile(void){
    if(lastFile != NULL){
        fclose(lastFile);
        lastFile = NULL;
    }
};

void createFile(char *name, char *dir){ // dir without / at the end.
    setWorkingPath();

    FILE * fp;
    errno = 0;

    char *filePath;
    asprintf(&filePath, "%s/%s/%s", workingPath, dir, name);
    fp = fopen(filePath, "wb");
    
    if( !fp ){
        perror(filePath);
        free(filePath);
        return;
    }
    fclose(fp);
    free(filePath);
}

long readFile(char *name, char *dir, char *buf){
    setWorkingPath();
    char *filePath;// = (char *)calloc(1000, sizeof(char));
    asprintf(&filePath, "%s/%s/%s", workingPath, dir, name);
    errno = 0;
    FILE *f;
    f = fopen(filePath , "rb");
    if( !f ){
        Log(ERROR, "fopen() fails %s", strerror(errno));
        return -1;
    }

    fseek(f , 0L , SEEK_END);
    long lSize = ftell(f);
    rewind(f);

    // buf = calloc(1, lSize+1);
    buf = calloc(lSize+1, sizeof(char));
    if( !buf ){
        fclose(f);
        fputs("calloc() fails", stderr);
        return -2;
    }

    errno = 0;
    if(1 != fread(buf , lSize, 1 , f)){
        fclose(f);
        free(buf);
        Log(ERROR, "fread() fails %s", strerror(errno));
        return -3;
    }

    fclose(f);
    free(filePath);
    return lSize;
}

void deleteFile(char *name){
    if (remove(name) == 0) {
        Log(INFO, "The file '%s' is deleted successfully.", name);
    } else {
        Log(ERROR, "The file '%s' is NOT deleted !?", name);
    }
}

// creating dir if not exist in working directory.
void createDirIfNotExist(char * dirName){
    setWorkingPath();
    char *path = (char *)calloc(500, sizeof(char));
    strcat(path, workingPath);
    strcat(path, "/");
    strcat(path, dirName);

    struct stat st = {0};

    if (stat(path, &st) == -1) {
        printf("creating dir: %s\n", path);
        errno = 0;
        int ret = mkdir(path, 0777);
        if (ret == -1) {
            perror("mkdir");
            printf("error mkdir..  %s", path);
            free(path);
            exit(EXIT_FAILURE);
        }
    }
    free(path);
}

bool fileExist(char * fileName, char * dir){
    setWorkingPath();
    char *filePath;
    asprintf(&filePath, "%s/%s/%s", workingPath, dir, fileName);
    if( access( filePath, F_OK ) != 0 ) { // file not found
        free(filePath);
        return false;
    }
    free(filePath);
    return true;
}


void SaveTtyOutput(char* message){
    if(fdt == NULL){
        char * filename = GenerateLogFile();
        char * path = (char *)calloc(500, sizeof(char));
        strcat(path, getWorkingPath());
        strcat(path, "/tty_log/tty_");
        strcat(path, filename);

        createDirIfNotExist("tty_log");

        printf("tty_log file will be in ->%s\n", path);

        //createFile(filename, "tty_log");

        fdt = fopen(path, "a+");
        if(fdt == NULL){
            printf("ERROR when opening file!!! filename:%s  errno: %s\n", path, strerror(errno));
        }
        free(path);
        free(filename);
    }
    if(fdt != NULL){
        int res = fprintf(fdt, "%s", message);
        if (res <= 0) {
            printf("Unable to write to file!! res:%d, errno:%s\n", res, strerror(errno));
            return;
        }
        fflush(fdt);
    }
}
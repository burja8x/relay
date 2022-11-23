#include "log.h"
#include "files.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char* path_log_pm3 = NULL;

char* getPm3LogPath(){
    if(path_log_pm3 == NULL){
        char * filename = GenerateLogFile();
        path_log_pm3 = (char *)calloc(300, sizeof(char));
        strcat(path_log_pm3, getWorkingPath());
        strcat(path_log_pm3, "/pm3_log/pm3_");
        strcat(path_log_pm3, filename);

        createDirIfNotExist("pm3_log");

        printf("pm3_log file will be in ->%s\n", path_log_pm3);
        return path_log_pm3;
    }else{
        return path_log_pm3;
    }
}
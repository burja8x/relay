// FIFO - Pipe
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "pipe.h"
#include "log.h"

#define MAX_BUF 1024*100

static const char *fifo_r = "/tmp/proxmark_relay_read";
static const char *fifo_w = "/tmp/proxmark_relay_write";

static int fdr;
static int fdw;
static int r, w;

// static uint8_t buf_r[MAX_BUF];

static uint8_t *buf_r = NULL;

bool PipeExist(const char *fifo){
    if(access(fifo, F_OK) != 0){
        Log(TRACE, "FIFO %s not exist.", fifo);
        return false;
    }
    return true;
}

void EnableNonBlocking(int fd){
    int oldflags = fcntl(fd, F_GETFL, 0);
    oldflags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, oldflags);
}

bool OpenPipe(bool nonBlocking){
    if(buf_r == NULL){
        buf_r = (uint8_t*)calloc(MAX_BUF, sizeof(uint8_t));
    }

    if(PipeExist(fifo_r) && PipeExist(fifo_w)){
        memset(buf_r, 0, MAX_BUF);
        fdr = open(fifo_w, O_RDONLY | O_NONBLOCK);//r
        fdw = open(fifo_r, O_WRONLY | O_NONBLOCK);//w
        Log(INFO ,"fdr: %d   fdw: %d", fdr, fdw);
        if(nonBlocking){
            EnableNonBlocking(fdr);
            //EnableNonBlocking(fdw);
        }
        return true;
    }else{
        return false;
    }
}

int GetData(uint8_t **data){
    memset(buf_r, 0, MAX_BUF);
    errno = 0;
    r = read(fdr, buf_r, MAX_BUF);
    // If no process has the pipe open for writing, read() shall return 0 to indicate end-of-file.
    // If some process has the pipe open for writing and O_NONBLOCK is set, read() shall return -1 and set errno to [EAGAIN].
    // If some process has the pipe open for writing and O_NONBLOCK is clear, read() shall block the calling thread until some
    //  data is written or the pipe is closed by all processes that had the pipe open for writing.
    *data = buf_r;

    if(r <= 0){
        if(r == 0){
            return 0;
        }
        else if(r == -1){
            return 0;
        }
        else{
            Log(ERROR ,"ERROR in pipe .. read is %d", r);
            Log(ERROR ,"errno: %s", strerror(errno));
            close(fdr);
            close(fdw);
            return -1;
        }
    }
    // printf("Received: '%s'  size: %d\n", buf_r, r);
    //printf("size: %d\n", r);
    return r;
}

bool SendToPipe(uint8_t *buf, size_t len){
    errno = 0;
    w = write(fdw, buf, len);
    if(w <= 0){
        if(w == -1){
            Log(WARNING, "write == -1 trying to open one more time.");
            close(fdw);
            fdw = open(fifo_r, O_WRONLY | O_NONBLOCK);
            w = write(fdw, buf, len);
            if(w > 0){
                return true;
            }
        }
        Log(ERROR, "ERROR in pipe .. write() is %d  len:%zu   This can happen if pm3 program is not running.", w, len);
        Log(ERROR ,"write() errno:%d  %s", errno, strerror(errno));
        return false;
    }else{
        return true; 
    }
}



bool CreatePipeIfNotExist(void){
    if( ! PipeExist(fifo_r)){
        mkfifo(fifo_r, 0666);
    }
    if( ! PipeExist(fifo_w)){
        mkfifo(fifo_w, 0666);
    }
    if( ! PipeExist(fifo_r) || ! PipeExist(fifo_w)){
        Log(ERROR, "Could not crate fifo !");
        return false;
    }
    return true;
}

void UnlinkPipe(void){
    if(PipeExist(fifo_r)){
        close(fdr);
        unlink(fifo_r);
    }
    if(PipeExist(fifo_w)){
        close(fdw);
        unlink(fifo_w);
    }
}

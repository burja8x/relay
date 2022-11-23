// FIFO - Pipe
#include "pipe.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "ui.h"
#include <errno.h>
#include "util_posix.h"

#define MAX_BUF 1024*100

#define ARR_SIZE 1024
static uint8_t *packetPositions[ARR_SIZE];
const char HexLookUp[] = "0123456789ABCDEF";

static const char *fifo_r = "/tmp/proxmark_relay_read";
static const char *fifo_w = "/tmp/proxmark_relay_write";

static int fdr;
static int fdw;
static int r, w;

static char buf_r[MAX_BUF];
static bool pipeOpen = false;

static PacketToPipe ptpTxt;

bool IsPipeOpen(void){
    // printf("pipe:%d\n", pipeOpen);
    return pipeOpen;
}

bool PipeExist(const char *fifo){
    if(access(fifo, F_OK) != 0){
        printf("FIFO not exist !\n");
        //PrintAndLogEx(FAILED, "FIFO %s not exist !\n", fifo);
        fflush(stdout);
        return false;
    }
    printf("TRUE\n");
    fflush(stdout);
    return true;
}

void EnableNonBlocking(int fd){
    int oldflags = fcntl(fd, F_GETFL, 0);
    oldflags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, oldflags);
}

bool OpenPipe(void){
    printf("Open pipe\n");
    if(PipeExist(fifo_r) && PipeExist(fifo_w)){
        memset(buf_r, 0, MAX_BUF);
        printf("Exist\n");
        fflush(stdout);
        fdw = open(fifo_w, O_WRONLY | O_NONBLOCK);
        printf("W\n");
        fflush(stdout);
        fdr = open(fifo_r, O_RDONLY | O_NONBLOCK);
        printf("fdr: %d   fdw: %d\n", fdr, fdw);
        fflush(stdout);
        pipeOpen = true;
        return true;
    }else{
        return false;
    }
}

void ClosePipe(void){
    close(fdr);
    close(fdw);
    pipeOpen = false;
}

int GetData(uint8_t **data){
    memset(buf_r, 0, MAX_BUF);
    errno = 0;
    r = read(fdr, buf_r, MAX_BUF);
    *data = (uint8_t *)buf_r;
    if(r <= 0){
        if(r == 0){
            return 0;
        }
        else if(r == -1){ // no data (if non blocking is enabled.)
            return 0;
        }else{
            PrintAndLogEx(FAILED, "ERROR in pipe .. read is %d\n", r);
            PrintAndLogEx(FAILED, "errno: %s", strerror(errno));
            close(fdr);
            close(fdw);
            pipeOpen = false;
            return -1;
        }
    }
    // printf("Received: '%s'  size: %d\n", buf, r);
    return r;
}

bool SendToPipe(uint8_t *buf, size_t len){
    w = write(fdw, buf, len);
    if(w <= 0){
        printf("write error");
        PrintAndLogEx(FAILED, "ERROR in pipe .. write is %d\n", w);
        return false;
    }else{
        return true; 
    }
}
bool SendPacketToPipe(PacketToPipe *ptp){
    uint16_t len = ptp->length+2+1;
    return SendToPipe((uint8_t *)ptp, len);
}
bool SendDataToPipe(const uint8_t cmd, uint16_t len, uint8_t *data){
    ptpTxt.cmd = cmd;
    ptpTxt.length = len;
    memcpy(ptpTxt.data, data, len);
    return SendToPipe((uint8_t *)&ptpTxt, len+3);
}
bool SendTxtToPipe(const char *txt){
    ptpTxt.cmd = P_INFO;
    ptpTxt.length = (uint16_t)strlen(txt);
    memcpy(ptpTxt.data, txt, ptpTxt.length);
    return SendToPipe((uint8_t *)&ptpTxt, strlen(txt)+3);
}

int WaitPipeDate(unsigned int timeout_ms, uint8_t **data){
    uint64_t start_time = msclock();
    while(1){
        int m = GetData(data);
        if(m == -1){
            return -1;
        }else if(m > 0){
            return m;
        }

        if((msclock() - start_time) > timeout_ms){
            PrintAndLogExPipe(FAILED, "timeout after %u ms", timeout_ms);
            return -2;
        }
    }
}

void getPackets(uint8_t *data, int length){
    memset(packetPositions, 0, ARR_SIZE * sizeof(uint8_t *));
    // for(int i = 0; i < length; i++){
    //     printf(" %c%c", HexLookUp[data[i] >> 4], HexLookUp[data[i] & 0x0F]);
    // }
    // printf("\n");
    uint8_t *d = data;
    int newLen = length;
    int x = 0;
    for(;;){
        if(newLen >= 3){
            uint16_t msgLen = d[0] | (d[1] << 8);
            uint8_t cmd = d[2];
            
            switch (cmd){
                case P_SNIFF:
                case P_INFO:
                case P_KILL:
                case P_LOG:
                case P_PING:
                case P_RELAY:{
                    if(msgLen + 3 <= newLen){
                        packetPositions[x++] = d;
                        newLen -= msgLen + 3;
                        d += msgLen + 3;
                        break;
                    }else{
                        PrintAndLogExPipe(FAILED, "msgLen + 3 <= newLen. msgLen:%u   newLen:%u.", msgLen, newLen);
                    }
                }
                default:{
                    PrintAndLogExPipe(FAILED, "ERROR unknown packet. cmd:%u   p.len:%u.", cmd, (unsigned int)msgLen);
                    for(int i = 0; i < length; i++){
                        printf(" %c%c", HexLookUp[data[i] >> 4], HexLookUp[data[i] & 0x0F]);
                    }
                    printf("\n");
                    return;
                    break;
                }
            }
        }else if(newLen == 0){
            return;
        }else{
            PrintAndLogExPipe(FAILED, "data len to small. len:%u", newLen);
        }
    }   
}
PacketToPipe* NextPacket(void){
    for(int i = ARR_SIZE-1; i >= 0; i--){
        if(packetPositions[i] == 0){

        }else{
            uint8_t *data = packetPositions[i];
            packetPositions[i] = 0;
            return (PacketToPipe*)data;
        }
    }
    return NULL;
}
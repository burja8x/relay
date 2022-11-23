#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include "files.h"
#include "pipe.h"
#include "ws.h"
#include "log.h"
#include "relay.h"

#define ARR_SIZE 1024

// const char HexLookUp[] = "0123456789ABCDEF";

// static int resultsLength[ARR_SIZE] = {0};
static uint8_t *packetPositions[ARR_SIZE];

static char writeToFile[ARR_SIZE] = {0}; 

static char * lastFile = NULL;
static char * lastDir = NULL;
static bool newSniff = false;

static char *lastRelayProxyFile = NULL;
static char *lastRelayMoleFile = NULL;

struct timeval startTime1, endTime1;

// for updating UI
bool anyNewMsg(){
    if(newSniff){
        newSniff = false;
        return true;
    }
    return false;
}

bool readSniff(uint8_t *data, int offset, bool print){
    memset(writeToFile, 0, ARR_SIZE * sizeof(char));
    
    uint16_t len;
    unsigned int startTime;
    unsigned int endTime;
    uint8_t parity;
    uint8_t tag;
    if(data[2 + offset] == 0x02){ // sniff
        // len += 3;
        offset += 3;
    }
    len = data[0 + offset] | (data[1 + offset] << 8);
    len -= 10;
    // Log(INFO, "len %u", len);
    // uint8_t cmd = data[2 + offset];
    startTime = data[3 + offset] | (data[4 + offset] << 8) | (data[5 + offset] << 16) | (data[6 + offset] << 24);
    endTime = data[7 + offset] | (data[8 + offset] << 8) | (data[9 + offset] << 16) | (data[10 + offset] << 24);
    parity = data[11 + offset];
    tag = data[12 + offset];

    // if(tag){
    //     if(print) printf("T: ");
    //     //strcat(writeToFile, "T");
    // }else{
    //     if(print) printf("D: ");
    //     //strcat(writeToFile, "D");
    // }

    // if(print) printf("%-8d ", endTime - startTime);
    // if(print) printf("%s %d %u %u %d %s\n",tag?"T":"D", len, startTime, endTime, parity, hex_text(&data[13+offset], len));
    sprintf(writeToFile, "%s %d %u %u %d %s\n", tag?"T":"R", len, startTime, endTime, parity, hex_text(&data[13+offset], len));
    if(print) Log(INFO,"%.*s", strlen(writeToFile)-2, writeToFile);
    // for(int i = 0; i < len; i++){
    //     if(print) printf(" %c%c", HexLookUp[data[13 + i + offset] >> 4], HexLookUp[data[13 + i  + offset] & 0x0F]);
    //     sprintf(writeToFile, "%s %c%c", writeToFile, HexLookUp[data[13 + i + offset] >> 4], HexLookUp[data[13 + i  + offset] & 0x0F]);
    // }

    // if(print) printf("\n");
    // strcat(writeToFile, "\n");
    // printf("strlen %lu\n",strlen(writeToFile));
    fflush(stdout);
    return true;
}

char* GenerateFileName(char * beginning, int num){
  char text[100];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  strftime(text, sizeof(text)-1, "%m%d", t);

  char* endStr;
  if(0 > asprintf(&endStr, "%s_%s-%d.data", beginning, text, num)) return NULL;
  Log(TRACE, "NAME: %s", endStr);

  return endStr;
}

void createDataFile(char * dir, char * typeStr){
    createDirIfNotExist(dir);
    int ii = 0;
    char * fileName = NULL;
    while(1){
        fileName = GenerateFileName(typeStr, ii++);
        if( ! fileName){ // if null
            fileName = (char *)calloc(50, sizeof(char));
            srand(time(NULL));
            sprintf(fileName, "%s-%d.data", typeStr, rand());
        }
        if( ! fileExist(fileName, dir) ) { // file not found
            break;
        }
    }
    Log(TRACE ,"Creating file %s", fileName);
    createFile(fileName, dir);
    lastFile = fileName;
    lastDir = dir;
    Log(TRACE, "File created.");
    fflush(stdout);
}

void save(uint8_t * pipe_data, bool print){
    readSniff(pipe_data, 0, print);
    if(lastFile == NULL) 
        createDataFile("history", "unknown");
    appendToFile(lastFile, lastDir, writeToFile, strlen(writeToFile));   
}
void saveRelayTrace(bool mole, uint8_t * pipe_data, bool print){
    if(lastRelayMoleFile == NULL || lastRelayProxyFile == NULL){Log(ERROR, "Relay proxy/mole file not exist?!"); return;}
    readSniff(pipe_data, 0, print);
    if(mole){
        lastFile = lastRelayMoleFile;
    }else {
        lastFile = lastRelayProxyFile;
    }
    appendToFile(lastFile, lastDir, writeToFile, strlen(writeToFile));
    newSniff = true;
}

void clearLastFile(void){
    free(lastFile);
    lastDir = NULL;
    lastFile = NULL;
}

char * getLastFileName(void){
    return lastFile;
}


void getPackets(uint8_t *data, int length){
    memset(packetPositions, 0, ARR_SIZE * sizeof(uint8_t *));
    uint8_t *d = data;
    int newLen = length;
    int x = 0;
    for(int i = 0; i < 1000;i++){
        if(newLen >= 3){
            uint16_t msgLen = d[0] | (d[1] << 8);
            uint8_t cmd = d[2];

            switch (cmd)
            {
            case P_RELAY: // priority RELAY
                if(d[3] == RELAY_START){
                    sendArrayOfData();
                }
                Send_WS(d, msgLen + 3);
                Log(TRACE, "P_RELAY");
                if(msgLen + 3 <= newLen){
                    newLen -= msgLen + 3;
                    d += msgLen + 3;
                }
                break;
            case P_SNIFF:
            case P_INFO:
            case P_KILL:
            case P_LOG:
            case P_PING:
            case P_RELAY_PROXY_TRACE:
            case P_RELAY_MOLE_TRACE:
                if(msgLen + 3 <= newLen){
                    packetPositions[x++] = d;
                    newLen -= msgLen + 3;
                    d += msgLen + 3;
                }
                break;
                // Log(FATAL ,"This packet is not expected to be handled. cmd:%u   p.len:%u", cmd, msgLen);
            default:
                Log(ERROR ,"Unknown packet. cmd:%u   p.len:%u", cmd, msgLen);
                printf("%s\n", hex_text(data, length));
                printf("%s\n", hex_text(d, newLen));
                return;
                break;
            }
        }
        else if(newLen == 0)
        {
            break;
        }else{
            Log(ERROR, "getPackets(); data len to small.");
            break;
        }
    }   
}
bool startsWith(const char *pre, const char *str) {
  return strncmp(pre, str, strlen(pre)) == 0;
}

PacketToPipe* NextPacket(void){
    for(int i = 0; i < ARR_SIZE; i++){
    //for(int i = ARR_SIZE-1; i >= 0; i--){
        if(packetPositions[i] == 0){

        }else{
            uint8_t *data = packetPositions[i];
            packetPositions[i] = 0;
            return (PacketToPipe*)data;
        }
    }
    return NULL;
}


// PROXY only
void handleNewPackets(void){
    for(int i = 0; i < ARR_SIZE; i++){
        if(packetPositions[i] == 0){

        }else{
            // gettimeofday(&startTime1, NULL);
            uint8_t *data = packetPositions[i];
            uint16_t msgLen = data[0] | (data[1] << 8);
            uint8_t cmd = data[2];
            // printf("i:%d  msgLen:%u  cmd:%u\n", i, msgLen, cmd);
            if(cmd == P_INFO){
                //print of cmd
                data += 3;
                Log(TRACE , "[PIPE] %.*s", msgLen, data);

                if(startsWith("--START SNIFF--", (char *)data)){
                    createDataFile("history", "sniff");
                }else if(startsWith("--END SNIFF--", (char *)data)){
                    clearLastFile();
                }else if(startsWith("--START RELAY--", (char *)data)){
                    bool a = SendToPipe(data-3, msgLen + 3);
                    if( ! a){
                        continue;
                    }
                    Log(INFO, "Relay started");
                    createDataFile("history", "proxy");
                    lastRelayProxyFile = lastFile;
                    createDataFile("history", "mole");
                    lastRelayMoleFile = lastFile;
                }else if(startsWith("--END RELAY--", (char *)data)){
                    clearLastFile();
                }
                else{
                    Log(ERROR, "NOT valid massage len: %d  data: '%s'", msgLen, (char *)data);
                }

            }else if(cmd == P_SNIFF){
                save(data, true);
                newSniff = true;
            }else if(cmd == P_RELAY_PROXY_TRACE){
                saveRelayTrace(false, data, true);
            }else if(cmd == P_RELAY){
                if(data[3] == RELAY_START){
                    sendArrayOfData();
                }
                Send_WS(data, msgLen + 3);
                Log(TRACE, "P_RELAY");
            }else if(cmd == P_KILL){
                Send_WS(data, msgLen + 3);
            }else if(cmd == P_PING){ // relay PING
                Send_WS(data, msgLen + 3);
            }else if(cmd == P_SEND_BACK){
                data[2] = P_SEND_BACK_2;
                SendToPipe(data, msgLen + 3);
            }else if(cmd == P_SEND_BACK_2){
                Log(INFO ,"%s\n", data+3);
            }else if(cmd == P_LOG){ // LOG [3]=who, [4]=type(info, warning, error, failed)
                uint8_t from_who = data[3];
                uint8_t log_level = data[4];
                if(data[3] == 0){
                    from_who = 5;
                }else if(data[3] == 1){
                    from_who = 4;// proxy pm client
                }
                AppendToLogFile(from_who, log_level, (char*)(data+5), msgLen-2);
            }
            else{
                Log(ERROR, "hendleNewPackets() cmd not found (%u)", cmd);
                return;
            }
            // gettimeofday(&endTime1, NULL);
            // long micros = endTime1.tv_usec - startTime1.tv_usec;
            // Log(TRACE ,"t(%u)  %lu us", cmd, micros);
        }
    }
    // memset(packetPositions, 0, ARR_SIZE*sizeof(uint8_t *));
}

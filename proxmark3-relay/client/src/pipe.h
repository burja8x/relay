//
#ifndef PIPE_H__
#define PIPE_H__

#include "common.h"

#define P_INFO 1
#define P_SNIFF 2
#define P_RELAY 3
#define P_PING 4
#define P_SEND_BACK 5
#define P_SEND_BACK_2 6
#define P_KILL 7
#define P_LOG 8
#define P_RELAY_PROXY_TRACE 9
#define P_RELAY_MOLE_TRACE 10

typedef struct {
    uint16_t length;
    uint8_t  cmd;
    uint8_t  data[512];
} PacketToPipe;

bool IsPipeOpen(void);
bool PipeExist(const char *fifo); 
void EnableNonBlocking(int fd);

bool OpenPipe(void);
void ClosePipe(void);
int GetData(uint8_t **data);
bool SendToPipe(uint8_t *buf, size_t len);
int WaitPipeDate(unsigned int timeout_ms, uint8_t **data);
bool SendPacketToPipe(PacketToPipe *ptp);
bool SendTxtToPipe(const char *txt);
bool SendDataToPipe(const uint8_t cmd, uint16_t len, uint8_t *data);

void getPackets(uint8_t *data, int length);
PacketToPipe* NextPacket(void);

#endif
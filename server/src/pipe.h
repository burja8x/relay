//
#include <stdbool.h>

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
#define P_POWEROFF 11

typedef struct {
    uint16_t length;
    uint8_t cmd;
    uint8_t  data[512];
} PacketToPipe;

bool PipeExist(const char *fifo); 
void EnableNonBlocking(int fd);

bool OpenPipe(bool nonBlocking);
int GetData(uint8_t **data);
int GetPipePacket(PacketToPipe *ptp);

bool SendToPipe(uint8_t *buf, size_t len);

bool CreatePipeIfNotExist(void);
void UnlinkPipe(void);

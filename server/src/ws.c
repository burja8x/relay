#include "mongoose.h"
#include "pipe.h"

static int isNull = 0;
static struct mg_connection *con = NULL;
static struct mg_mgr *mgr = NULL;
static PacketToPipe ptp;
void SetC(struct mg_connection *c){
    con = c;
}
void SetM(struct mg_mgr *m){
    mgr = m;
}
void Send_WS(uint8_t *data, int len){
    if (con == NULL){
        if(isNull == 0){
            printf("mg_connection is NULL.\n");
        }
        isNull = 1;
        return;
    }

    //printf("<WS> -> cmd:%u  total len:%d\n", data[2], len);
    mg_ws_send(con, (char *)data, len, WEBSOCKET_OP_BINARY);
    isNull = 0;
    if((data[2] == P_RELAY || data[2] == P_RELAY_MOLE_TRACE || data[2] == P_PING) && mgr != NULL){ 
        mg_mgr_poll(mgr, 0);
    }
}

void SendText_WS(char *text, uint8_t cmd){
    ptp.cmd = cmd;
    ptp.length = strlen(text);
    memcpy(ptp.data, (uint8_t*)text, ptp.length);
    Send_WS((uint8_t *)&ptp, strlen(text)+3);
}

bool Is_WS_Null(){
    return con == NULL;
}
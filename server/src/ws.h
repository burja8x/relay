#include "mongoose.h"

void SetC(struct mg_connection *c);
void SetM(struct mg_mgr *m);

void Send_WS(uint8_t *data, int len);
void SendText_WS(char *text, uint8_t cmd);
bool Is_WS_Null();
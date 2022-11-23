#include "common.h"
#include "mifare.h"

// typedef struct{
//     uint8_t type;
//     uint16_t ms;
//     uint8_t extra;
//     uint8_t flags;
// } relay_time4;

typedef struct relay_time{
    uint16_t ms;
    uint8_t flags;
    uint8_t wtxm;
    uint8_t bytes_len;
    uint8_t* bytes;
    uint8_t* x_bytes;
} relay_time;

void RelayIso14443a(uint8_t param);
// void Relay_Proxy_SetTestTime4(uint8_t *data);
void Relay_Proxy_SetTestTime(uint8_t *data);
bool Set_Time_Test(uint8_t *rx, uint16_t rx_len);
// bool Do_Time_Test4(void);
// bool ActOnRespReady_TestTime4(uint8_t *rx, uint16_t *rx_len, bool addCRC);
int DoChaining(void);
bool Relay_Proxy_PrecompiledTagInfo(iso14a_card_select_t *card, tag_response_info_t **responses);
// bool Relay_Proxy_ResponseInit_Iso14443a_Dynamic(iso14a_card_select_t *card, tag_response_info_t **responses2);
bool Relay_Proxy_SendResponse(uint8_t *data, uint16_t len, bool addCRC);
int Proxy_AutoResponse(tag_response_info_t *responses, tag_quick_response_info_t *responses2,  iso14a_card_select_t *card, uint8_t *receivedCmd, int len);

bool Relay_Proxy_PrecompiledResp_ATS(iso14a_card_select_t *card, tag_quick_response_info_t *responses_init);
bool Relay_Proxy_PrecompiledResp_Dynamic_AddQuickResp(tag_quick_response_info_t *responses_init, uint8_t *data);
bool addQuickReply(tag_quick_response_info_t *responses_init);

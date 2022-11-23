
#define RELAY_START 1
#define RELAY_TAG_INFO 3
#define RELAY_RAW 4
#define RELAY_REPLY_RAW 5
#define RELAY_PROXY_END 7
#define RELAY_PROXY_TRACE 9
#define RELAY_SET_CHANGE 13
#define RELAY_SET_INSERT 12
#define RELAY_SET_QUICK_REPLY 14
#define RELAY_SET_TEST_TIME4 15
#define RELAY_SET_TEST_TIME 16

typedef struct {
    uint8_t uid[10];
    uint8_t uidlen;
    uint8_t atqa[2];
    uint8_t sak;
    uint8_t ats_len;
    uint8_t ats[256];
} iso14a_card_select_t;

typedef struct relay_change_data{
    uint8_t what_len;
    uint8_t tag_prev; //b1 = tag, b0 = prev 1st
    uint8_t replace_with_len;
    uint8_t addCRC_setBN; //b1 = crc, b0 = BN
    // uint8_t x_what_len;
    // uint8_t x_replace_with_len;
    uint8_t prev_len;
    uint8_t* what;
    uint8_t* replace_with;
    uint8_t* x_what;
    uint8_t* x_replace_with;
    uint8_t* prev;
    uint8_t* x_prev;
} relay_change_data;

typedef struct relay_insert_data{
    uint8_t send_len;
    uint8_t tag_rec_len;
    uint8_t tag_resp_len;
    uint8_t flags;
    uint8_t* send;
    uint8_t* tag_rec;
    uint8_t* tag_resp;
    uint8_t* x_tag_rec;
    uint8_t* x_tag_resp;
} relay_insert_data;

typedef struct relay_time{
    uint16_t ms;
    uint8_t flags;
    uint8_t wtxm;
    uint8_t bytes_len;
    uint8_t* bytes;
    uint8_t* x_bytes;
} relay_time;


void parseStartRelay(int length, char* data);
uint8_t* setNewValues(uint8_t* in_mole_card);
void sendArrayOfData(void);
#include "iso14443a.h"
#include "string.h"
#include "proxmark3_arm.h"
#include "cmd.h"
#include "appmain.h"
#include "BigBuf.h"
#include "fpgaloader.h"
#include "ticks.h"
#include "util.h"
#include "parity.h"
#include "mifareutil.h"
#include "commonutil.h"
#include "crc16.h"
#include "protocols.h"
#include <stdio.h>
#include "proxy.h"
#include "dbprint.h"

// for Print text
#define MAX_STR 512
static char* hex_text(uint8_t* buf, int len){
    static char hex[MAX_STR] = {0};
    memset(hex, 0, MAX_STR);
    char *tmp = (char *)hex;

    for (int i = 0; i*3 < MAX_STR && i < len; ++i, tmp += 3) {
        sprintf(tmp, "%02X ", (unsigned int) buf[i]);
    }

    *tmp = '\0';
    return hex;
}

static void SendTrace(void){
    static uint8_t data_s[PM3_CMD_DATA_SIZE] = { 0x00 };
    uint8_t *trace = BigBuf_get_addr();
    uint16_t traceLen = BigBuf_get_traceLen();

    if (traceLen != 0) {
        uint16_t tracepos = 0;
        uint32_t endTime;
        while (tracepos < traceLen) {
            tracelog_hdr_t *hdr = (tracelog_hdr_t *)(trace + tracepos);
            tracepos += TRACELOG_HDR_LEN + hdr->data_len + TRACELOG_PARITY_LEN(hdr);
            if(hdr->data_len == 0){
                continue;
            }
            data_s[0] = ((hdr->data_len+10) >> 0) & 0xFF;
            data_s[1] = ((hdr->data_len+10) >> 8) & 0xFF;
            data_s[2] = 0x09; // RELAY_PROXY_TRACE
            data_s[3] = (hdr->timestamp >> 0) & 0xFF;
            data_s[4] = (hdr->timestamp >> 8) & 0xFF;
            data_s[5] = (hdr->timestamp >> 16) & 0xFF;
            data_s[6] = (hdr->timestamp >> 24) & 0xFF;
            endTime = hdr->timestamp + hdr->duration;
            data_s[7] = (endTime >> 0) & 0xFF;
            data_s[8] = (endTime >> 8) & 0xFF;
            data_s[9] = (endTime >> 16) & 0xFF;
            data_s[10] = (endTime >> 24) & 0xFF;
            data_s[11] = 0; // crc
            data_s[12] = hdr->isResponse?1:0; // TAG = 1
            for(int i = 0; i < hdr->data_len; i++){
                data_s[i+13] = hdr->frame[i];
            }
            // Dbprintf("len: %u   %X  time:%u", hdr->data_len, data_s[13], hdr->timestamp);
            reply_mix(CMD_HF_ISO14443A_RELAY, 9, 0, 0, &data_s[0], hdr->data_len+13); // RELAY_PROXY_TRACE
        }
    }
    clear_trace();
}

/*
    // I-block PCB R-block PCB S-block PCB
    // RATS (11100000)b 
    // PPS  (1101xxxx)b
    // I-block (00xxxxxx)b (not (00xxx101)b) // not 05, 0D, 15, 1D
    // R-block (10xxxxxx)b (not (1001xxxx)b) // not 9X
    // S-block (11xxxxxx)b (not (1110xxxx)b and not (1101xxxx)b) // not EX, DX
    // I-block 000xxx1x  01-1F
    // R-block 101xx01x  A2-BB
    // S-block 11xxx0x0  C0-FA .... CX, FX     F2(wtx)
*/

static bool is_13334_4_I_block(uint8_t *data){
    if((data[0] & 0xE0) == 0 && (data[0] & 0x02) == 2){
        return true;
    }
    return false;
}
static bool is_13334_4_R_block(uint8_t *data){
    if((data[0] & 0xE0) == 0xA0 && (data[0] & 0x06) == 2){
        return true;
    }
    return false;
}
/*static bool is_13334_4_S_block(uint8_t *data){
    if((data[0] & 0xC0) == 0xC0){
        return true;
    }
    return false;
}*/
static bool is_13334_4_WTX(uint8_t *data, uint16_t len){
    // 1111X010 = F2 ali F9(CID)
    if((data[0] & 0xF7) == 0xF2 && len == 4){
        return true;
    }
    return false;
}
static bool is_13334_4_Deselect(uint8_t *data, uint16_t len){
    // 1100X010 = C2 ali C9(CID)
    if((data[0] & 0xC7) == 0xC2 && len == 3){
        return true;
    }
    return false;
}



static uint8_t *prev_cmd = NULL;
static uint16_t prev_cmd_len = 0;
static uint8_t *prev_resp = NULL;
static uint16_t prev_resp_len = 0;
static uint16_t fsd = 256; // default ... reader can get max 32bytes frame size. 

static uint8_t count_quick_resp = 0;
static bool waiting_block_num_02 = true;
static bool waitingForMole = false;
// static bool ignoreNextCmd = false;
static bool tag_type_14443_4 = false;

static uint8_t chainingBuf[MAX_FRAME_SIZE] = { 0x00 };
static uint8_t chainingPos = 0;
static uint8_t chainingBufLen = 0;
static uint8_t chainingFirstByte = 0;

// static relay_time4 *test_time4 = NULL;
// static uint8_t test_time4_size = 0;
// static int8_t test_time4_step = 0;
// static bool test_time4_r_block_rec = false;
// static bool test_time4_done = false;

static relay_time *test_time = NULL;
static uint8_t *send_later = NULL;
static uint16_t send_later_len = 0;
static uint32_t start_time_test = 0;
static uint32_t diff_time_test = 0;
static uint32_t tmp_diff_time_test = 0;

void RelayIso14443a(uint8_t param){
    Dbprintf("..Relay..");
    iso14a_card_select_t card;
    tag_response_info_t *responses;
    uint8_t receivedCmd[MAX_FRAME_SIZE] = { 0x00 };
    uint8_t receivedCmdPar[MAX_PARITY_SIZE] = { 0x00 };

    int len = 0;
    uint16_t ignore_every_n = 0;

    iso14443a_setup(FPGA_HF_ISO14443A_TAGSIM_LISTEN);
    iso14a_set_timeout(201400); // 106 * 19ms default *100?
    clear_trace();
    set_tracing(true);
    StartCountUS();
    LED_A_OFF();
    LED_B_OFF();
    LED_C_OFF();
    LED_D_OFF();
    prev_cmd = BigBuf_calloc(256);
    prev_resp = BigBuf_calloc(256);
    
#define MAX_QUICK_RESP 20
    tag_quick_response_info_t *responses2 = (tag_quick_response_info_t*)BigBuf_calloc(sizeof(tag_quick_response_info_t) * MAX_QUICK_RESP);

    uint16_t count_tag_info_packets = 0;

    uint32_t time_in = 0;

    // uint32_t time_test2 = 0;

    for(;;){
        WDT_HIT();
        if (BUTTON_PRESS()){
            Dbprintf("BUTTON_PRESS()");
            goto proxy_end;
        }
        
        // time_test2 = GetCountUS();
        PacketCommandNG rx;
        memset(&rx.data, 0, sizeof(rx.data));
        int ret = receive_ng(&rx); // Get CMD from USB.
        // Dbprintf("time receive_ng: %u us", (GetCountUS() - time_test2));

        if (ret == PM3_SUCCESS) {
            if(rx.cmd == CMD_BREAK_LOOP){
                goto proxy_end_2;
            }else if(rx.cmd == CMD_PING){
                reply_ng(CMD_HF_ISO14443A_RELAY, PM3_SNONCES, rx.data.asBytes, rx.length);
            }else if(rx.cmd == CMD_HF_ISO14443A_RELAY && rx.oldarg[0] == 3){ // RELAY_TAG_INFO // CARD INFO
                if(memcmp(&card, rx.data.asBytes, 15) != 0 || count_tag_info_packets == 0){ // if is the same as last time 
                    Dbprintf("Got CARD info. COPY. %u", rx.length);
                    memcpy(&card, (iso14a_card_select_t *)rx.data.asBytes, rx.length);
                    if(card.ats_len > 1){
                        tag_type_14443_4 = true;
                    }else{
                        tag_type_14443_4 = false;
                    }
                    if (Relay_Proxy_PrecompiledTagInfo(&card, &responses) == false) {
                        Dbprintf("ERROR in Relay_Proxy_PrecompiledTagInfo() !");
                        goto proxy_end;
                    }
                    if (Relay_Proxy_PrecompiledResp_ATS(&card, &responses2[count_quick_resp++]) == false){
                        Dbprintf("ERROR in Relay_Proxy_PrecompiledResp_ATS() !");
                        goto proxy_end;
                    }
                }
                // init = true;
                Dbprintf("Card responses ready. BigBuf size:%u", BigBuf_get_size());
                count_tag_info_packets++;
                LED_A_ON();
            }else if(rx.cmd == CMD_HF_ISO14443A_RELAY && rx.oldarg[0] == 5){ // RELAY_REPLY_RAW
                // Respond form mole (real Tag).
                // time_test2 = GetCountUS();
                LED_B_ON();
                waitingForMole = false;

                if(tag_type_14443_4 && rx.length > fsd){ // if packet is to big for reader... DO Chaining
                    Dbprintf("!!!! Do Chaining !!!!");
                    memcpy(chainingBuf, rx.data.asBytes, rx.length);
                    chainingBufLen = rx.length;
                    chainingPos = 0;
                    chainingFirstByte = chainingBuf[0];

                    DoChaining();
                    goto jump_get_reader_cmd;
                }

                // Block Number correction 
                bool addCRC = false;
                if(tag_type_14443_4 && (is_13334_4_I_block(rx.data.asBytes) || is_13334_4_R_block(rx.data.asBytes))){ // Correct Block num.
                    if((waiting_block_num_02 && (rx.data.asBytes[0] & 3) == 3) || (waiting_block_num_02 == false && (rx.data.asBytes[0] & 3) == 2)){
                        rx.data.asBytes[0] ^= 1;
                        addCRC = true;
                        rx.length -= 2;
                    }
                }

                if(test_time != NULL){
                    if(Set_Time_Test(rx.data.asBytes, rx.length)){
                        if((GetCountUS()-start_time_test) >= test_time->ms * 1000){ // ok send now.
                            Dbprintf("(test_time): Too late... now:%u ms   waiting:%u ms", (GetCountUS()-start_time_test)/1000, test_time->ms);
                        }else{ // dont send... yet
                            diff_time_test = test_time->ms * 1000;
                            if(addCRC){
                                AddCrc14A(rx.data.asBytes, rx.length);
                                rx.length += 2;
                            }
                            memcpy(send_later, rx.data.asBytes, rx.length);
                            send_later_len = rx.length;
                            Dbprintf("(test_time): WAITING  now:%u ms   waiting:%u ms", (GetCountUS()-start_time_test)/1000, test_time->ms);
                            goto jump_get_reader_cmd;
                        }
                    }
                }

                Relay_Proxy_SendResponse(rx.data.asBytes, rx.length, addCRC);
                // Dbprintf("time send_resp: %u us", (GetCountUS() - time_test2));
                LED_B_OFF();
                LED_C_OFF();

                Dbprintf("From mole %02X %02X  %s  time: %u ms", rx.data.asBytes[0], rx.data.asBytes[1], (addCRC?"(CRC Fix)":""), ((GetCountUS() - time_in) / 1000));
                // SendTrace();
            }else if(rx.cmd == CMD_HF_ISO14443A_RELAY && rx.oldarg[0] == 14 && rx.length >= 10){ // RELAY_SET_QUICK_REPLY
                Relay_Proxy_PrecompiledResp_Dynamic_AddQuickResp(&responses2[count_quick_resp++], rx.data.asBytes);
            // }else if(rx.cmd == CMD_HF_ISO14443A_RELAY && rx.oldarg[0] == 15 && rx.length >= 4){ // RELAY_SET_TEST_TIME4
            //     Relay_Proxy_SetTestTime4(rx.data.asBytes);
            }else if(rx.cmd == CMD_HF_ISO14443A_RELAY && rx.oldarg[0] == 16 && rx.length >= 4){ // RELAY_SET_TEST_TIME
                Relay_Proxy_SetTestTime(rx.data.asBytes);
            }else{
                Dbprintf("ERROR unknown cmd: %X   relay cmd:%lu  len:%u", rx.cmd, rx.data.asBytes[0], rx.length);
                reply_ng(CMD_HF_ISO14443A_RELAY, PM3_EFAILED, NULL, 0);
            }
        } else if (ret != PM3_ENODATA) {
            Dbprintf("(Proxy) ERROR in frame reception: %d %s", ret, (ret == PM3_EIO) ? "PM3_EIO" : "");
            goto proxy_end;
        } else {
            // NO DATA
        }

jump_get_reader_cmd:
        WDT_HIT();
        tmp_diff_time_test = 0;
        // if(test_time || test_time4_r_block_rec){tmp_diff_time_test = diff_time_test;}
        if(test_time){tmp_diff_time_test = diff_time_test;}
        
        if (GetIso14443aCommandFromReaderInTime(receivedCmd, receivedCmdPar, &len, start_time_test, tmp_diff_time_test) == false) {
            // button press or USB data or time-up
        }else{ // 14443 DATA
            LED_D_ON();
            time_in = GetCountUS();
            
            //Dbprintf("e %02X   %d", receivedCmd[0], len);
            int dds = Proxy_AutoResponse(responses, responses2, &card, receivedCmd, len);
            Dbprintf("r %02X   %d   dds:%d time_auto_r: %u us", receivedCmd[0], len, dds, (GetCountUS() - time_in));
            LED_D_OFF();
            
            //if(receivedCmd[0] != 0xB2 && receivedCmd[0] != 0xB3){ // ... 14443-4 send again. 
            memcpy(prev_cmd, receivedCmd, len);
            prev_cmd_len = len;
            //}

            if(dds == 0){// SEND cmd to MOLE (real Tag) 
                if(len <= 2){ // Most likely error in transmition. // ignore
                    Dbprintf("Error got cmd. len:%u <= 2 ... %02X  ... IGNORE !", len, receivedCmd[0]);
                    goto jump_get_reader_cmd;
                }
                
                if(tag_type_14443_4 && is_13334_4_I_block(receivedCmd) && len >= 3 && ! check_crc(CRC_14443_A, receivedCmd, len)){
                    Dbprintf("!!! CRC NOT OK !!! %02X  len:%u", receivedCmd[0], len);
                    // TO DO send something else NAK ????
                    if(tag_type_14443_4){
                        // do not send !!! // ignore
                        goto jump_get_reader_cmd;
                    }
                }
                if(test_time != NULL){ 
                    start_time_test = GetCountUS();
                }

                ignore_every_n++;
                
                if(waitingForMole){
                    Dbprintf("We have new cmd from Reader, but we didnt received reply from mole.");
                    if(ignore_every_n%2 == 0){
                        Dbprintf("IGNORING... %02X .. len:%u", receivedCmd[0], len);
                        goto jump_get_reader_cmd;
                    }
                }
                LED_C_ON();
                
                // time_test2 = GetCountUS();
                reply_mix(CMD_HF_ISO14443A_RELAY,4,0,0, receivedCmd, len); // RELAY_RAW
                // Dbprintf("time reply_mix: %u us", (GetCountUS() - time_test2));
                LogTrace2(receivedCmd,
                    GetUart14a()->len,
                    GetUart14a()->startTime * 16 - DELAY_AIR2ARM_AS_TAG,
                    GetUart14a()->endTime * 16 - DELAY_AIR2ARM_AS_TAG,
                    GetUart14a()->parity,
                    true);

                waitingForMole = true;

                //if I or R block save block_number
                waiting_block_num_02 = false;
                if((is_13334_4_I_block(receivedCmd) || is_13334_4_R_block(receivedCmd)) && (receivedCmd[0] & 0x01) == 0){ // if 02
                    waiting_block_num_02 = true;
                }
                // time_test2 = GetCountUS();
                SendTrace();
                // Dbprintf("send_trace: %u us", (GetCountUS() - time_test2));
            } else if(dds > 0){
                goto jump_get_reader_cmd;
                // OK 
            } else if(dds == -10){
                goto jump_get_reader_cmd;
                // do nothing
            } else if(dds < 0){
                goto proxy_end;
            }
        }

        if(test_time != NULL && send_later_len && diff_time_test && GetCountUS()-start_time_test >= diff_time_test){ // Test time.
            Relay_Proxy_SendResponse(send_later, send_later_len, false);
            Dbprintf("(test_time): Sending now:%u ms   waiting:%u ms", (GetCountUS()-start_time_test)/1000, diff_time_test/1000);
            memset(send_later, 0, 256);
            send_later_len = 0;
            diff_time_test = 0;
        }
    }
proxy_end:
    Dbprintf("END");
    reply_mix(CMD_HF_ISO14443A_RELAY,7,0,0, NULL, 0); // RELAY_PROXY_END
proxy_end_2:
    SendTrace();
    switch_off();
    set_tracing(false);
    BigBuf_free_keep_EM();
}

bool Set_Time_Test(uint8_t *rx, uint16_t rx_len){
    bool do_test = true;
    if(tag_type_14443_4 && (test_time->flags & 2) == 2 && is_13334_4_I_block(rx)){ // only I-block
        do_test = true;
    }else{
        do_test = false;
    }
    if(test_time->bytes_len > 0){
        bool starts_with = (test_time->flags & 1) == 1;
        if((starts_with && rx_len >= test_time->bytes_len) 
            || ( starts_with == false && rx_len == test_time->bytes_len)){
            
            bool eq = false;
            for(int j = 0; j < test_time->bytes_len; j++){ // match tag_rec bytes.
                bool x_bit_L = (test_time->x_bytes[(j*2)/8] & (1 << ((j*2)%8) )); // X0
                bool x_bit_R = (test_time->x_bytes[((j*2)+1)/8] & (1 << (((j*2)+1)%8) )); // 0X 
                uint8_t in = rx[j];
                if(x_bit_L){
                    in &= 0x0F;
                }
                if(x_bit_R){
                    in &= 0xF0;
                }
                Dbprintf("t r: %02X  %02X    x_L:%u  x_R:%u", rx[j], test_time->bytes[j], x_bit_L, x_bit_R);
                if(in == test_time->bytes[j]){ // EQ
                    eq = true;
                }else{
                    Dbprintf("t r B:%d   %02X != %02X    x_L:%u  x_R:%u", j, rx[j], test_time->bytes[j], x_bit_L, x_bit_R);
                    eq = false;
                    break;
                }
            }
            if(eq){
                //Dbprintf("(test_time): %s", hex_text(rx.data.asBytes, rx.length));
                do_test = true;
            }else{
                do_test = false;
            }
        }else{
            do_test = false;
        }
    }
    return do_test;
}

void Relay_Proxy_SetTestTime(uint8_t *data){
    send_later = BigBuf_calloc(256);
    Dbprintf("(time): %u  %u  %u  %u", data[0] | (data[1] << 8), data[2], data[3], data[4]);
    test_time = (relay_time*)BigBuf_calloc(sizeof(relay_time));
    memcpy(test_time, data, 5);
    if(test_time->bytes_len != 0){
        test_time->bytes = BigBuf_calloc(test_time->bytes_len);
        memcpy(test_time->bytes, data+sizeof(relay_time), test_time->bytes_len);

        uint16_t x_bytes_len = (test_time->bytes_len*2/8);
        if(((test_time->bytes_len*2)%8) != 0) x_bytes_len++;

        test_time->x_bytes = BigBuf_calloc(x_bytes_len);
        memcpy(test_time->x_bytes, data+sizeof(relay_time)+test_time->bytes_len, x_bytes_len);
    }
    Dbprintf("(time): %u ms | flags: %u | wtxm: %u | bytes_len %u | %s |", test_time->ms, test_time->flags, test_time->wtxm ,test_time->bytes_len, hex_text(test_time->bytes, test_time->bytes_len));
}

int DoChaining(void){
    chainingBuf[chainingPos] = chainingFirstByte;
    if((waiting_block_num_02 && (chainingBuf[chainingPos] & 3) == 3) || (waiting_block_num_02 == false && (chainingBuf[chainingPos] & 3) == 2)){
        chainingBuf[chainingPos] ^= 1;
    }
    uint16_t nextPos = chainingPos + fsd - 3; // 3 == first byte + 2xCRC
    Dbprintf("C_len:%u  C_pos:%u  N_pos:%u  fsd:%u  firstB:%02X", chainingBufLen, chainingPos, nextPos, fsd, chainingFirstByte);
    waiting_block_num_02 ^= 1;
    if(nextPos+1 >= chainingBufLen-2){
        int ret = chainingBufLen-2-chainingPos+2;
        Dbprintf("Sending to reader:%s    len:%u", hex_text(&chainingBuf[chainingPos], chainingBufLen-2-chainingPos), chainingBufLen-2-chainingPos);
        Relay_Proxy_SendResponse(&chainingBuf[chainingPos], chainingBufLen-2-chainingPos, true); // last ?!
        if(chainingFirstByte & 0x10){ // chaining chaining
            uint8_t a2[] = {0xA2, 0xE6, 0xD7}; 
            Dbprintf("Chaining -> Chaining !!! sending:%02X ACK to mole(Tag).", a2[0]); 
            reply_mix(CMD_HF_ISO14443A_RELAY,4,0,0, a2, 3); // RELAY_RAW
        }
        chainingPos = 0;
        chainingBufLen = 0;
        return ret;
    }else{
        chainingBuf[chainingPos] |= 0x10;
        Dbprintf("Sending to reader:%s", hex_text(&chainingBuf[chainingPos], fsd-2));
        Relay_Proxy_SendResponse(&chainingBuf[chainingPos], fsd-2, true);
        chainingPos = nextPos;
        return fsd;
    }
}
/* "precompile" responses UID, ATQA, SAK
    Prepare the responses of the anticollision phase
    there will be not enough time to do this at the moment the reader sends it REQA */
bool Relay_Proxy_PrecompiledTagInfo(iso14a_card_select_t *card, tag_response_info_t **responses) {
    uint8_t sak = card->sak;
    // The first response contains the ATQA (note: bytes are transmitted in reverse order).
    static uint8_t rATQA[2] = { 0x00 };
    rATQA[0] = card->atqa[0];
    rATQA[1] = card->atqa[1];
    // The second response contains the (mandatory) first 24 bits of the UID
    static uint8_t rUIDc1[5] = { 0x00 };
    // For UID size 7,
    static uint8_t rUIDc2[5] = { 0x00 };
    // For UID size 10,
    static uint8_t rUIDc3[5] = { 0x00 };
    // Prepare the mandatory SAK (for 4, 7 and 10 byte UID)
    static uint8_t rSAKc1[3]  = { 0x00 };
    // Prepare the optional second SAK (for 7 and 10 byte UID), drop the cascade bit for 7b
    static uint8_t rSAKc2[3]  = { 0x00 };
    // Prepare the optional third SAK  (for 10 byte UID), drop the cascade bit
    static uint8_t rSAKc3[3]  = { 0x00 };
    // ATS, answer to RATS
    static uint8_t rRATS[] = { 0x06, 0x75, 0x77, 0x81, 0x02, 0x80, 0x00, 0x00 };
    AddCrc14A(rRATS, sizeof(rRATS) - 2);

    static uint8_t rPPS[3] = { 0xD0 };
    AddCrc14A(rPPS, sizeof(rPPS) - 2);

    if (card->uidlen == 4) {
        rUIDc1[0] = card->uid[0];
        rUIDc1[1] = card->uid[1];
        rUIDc1[2] = card->uid[2];
        rUIDc1[3] = card->uid[3];
        rUIDc1[4] = rUIDc1[0] ^ rUIDc1[1] ^ rUIDc1[2] ^ rUIDc1[3];

        rSAKc1[0] = sak & 0xFB;
        AddCrc14A(rSAKc1, sizeof(rSAKc1) - 2);
    } else if (card->uidlen == 7) {
        rUIDc1[0] = 0x88;  // Cascade Tag marker
        rUIDc1[1] = card->uid[0];
        rUIDc1[2] = card->uid[1];
        rUIDc1[3] = card->uid[2];
        rUIDc1[4] = rUIDc1[0] ^ rUIDc1[1] ^ rUIDc1[2] ^ rUIDc1[3];

        rUIDc2[0] = card->uid[3];
        rUIDc2[1] = card->uid[4];
        rUIDc2[2] = card->uid[5];
        rUIDc2[3] = card->uid[6];
        rUIDc2[4] = rUIDc2[0] ^ rUIDc2[1] ^ rUIDc2[2] ^ rUIDc2[3];

        rSAKc1[0] = 0x04;
        rSAKc2[0] = sak & 0xFB;
        AddCrc14A(rSAKc1, sizeof(rSAKc1) - 2);
        AddCrc14A(rSAKc2, sizeof(rSAKc2) - 2);
    } else if (card->uidlen == 10) {
        rUIDc1[0] = 0x88;  // Cascade Tag marker
        rUIDc1[1] = card->uid[0];
        rUIDc1[2] = card->uid[1];
        rUIDc1[3] = card->uid[2];
        rUIDc1[4] = rUIDc1[0] ^ rUIDc1[1] ^ rUIDc1[2] ^ rUIDc1[3];

        rUIDc2[0] = 0x88;  // Cascade Tag marker
        rUIDc2[1] = card->uid[3];
        rUIDc2[2] = card->uid[4];
        rUIDc2[3] = card->uid[5];
        rUIDc2[4] = rUIDc2[0] ^ rUIDc2[1] ^ rUIDc2[2] ^ rUIDc2[3];

        rUIDc3[0] = card->uid[6];
        rUIDc3[1] = card->uid[7];
        rUIDc3[2] = card->uid[8];
        rUIDc3[3] = card->uid[9];
        rUIDc3[4] = rUIDc3[0] ^ rUIDc3[1] ^ rUIDc3[2] ^ rUIDc3[3];

        rSAKc1[0] = 0x04;
        rSAKc2[0] = 0x04;
        rSAKc3[0] = sak & 0xFB;
        AddCrc14A(rSAKc1, sizeof(rSAKc1) - 2);
        AddCrc14A(rSAKc2, sizeof(rSAKc2) - 2);
        AddCrc14A(rSAKc3, sizeof(rSAKc3) - 2);
    } else {
        Dbprintf("[-] ERROR: UID size not defined");
        return false;
    }

    static tag_response_info_t responses_init[] = {
        { .response = rATQA,      .response_n = sizeof(rATQA)     },  // Answer to request - respond with card type
        { .response = rUIDc1,     .response_n = sizeof(rUIDc1)    },  // Anticollision cascade1 - respond with uid
        { .response = rUIDc2,     .response_n = sizeof(rUIDc2)    },  // Anticollision cascade2 - respond with 2nd half of uid if asked
        { .response = rUIDc3,     .response_n = sizeof(rUIDc3)    },  // Anticollision cascade3 - respond with 3rd half of uid if asked
        { .response = rSAKc1,     .response_n = sizeof(rSAKc1)    },  // Acknowledge select - cascade 1
        { .response = rSAKc2,     .response_n = sizeof(rSAKc2)    },  // Acknowledge select - cascade 2
        { .response = rSAKc3,     .response_n = sizeof(rSAKc3)    },  // Acknowledge select - cascade 3
        { .response = rRATS,      .response_n = sizeof(rRATS)     },
        { .response = rPPS,      .response_n = sizeof(rPPS)     },
    };
     
    #define ALLOCATED_TAG_MODULATION_BUFFER_SIZE_2 400
    // size_t free_buffer_size = (34*9) + 8*3 //sum_bytes*9 + num_responses*3 = 330;
    uint8_t *free_buffer = BigBuf_malloc(ALLOCATED_TAG_MODULATION_BUFFER_SIZE_2);
    uint8_t *free_buffer_pointer = free_buffer;
    size_t free_buffer_size = ALLOCATED_TAG_MODULATION_BUFFER_SIZE_2;
    for (size_t i = 0; i < ARRAYLEN(responses_init); i++) {
        if (prepare_allocated_tag_modulation(&responses_init[i], &free_buffer_pointer, &free_buffer_size) == false) {
            BigBuf_free_keep_EM();
            Dbprintf("Not enough modulation buffer size, exit after %d elements", i);
            return false;
        }
    }
    Dbprintf("free_buffer_size:%u", (unsigned int)free_buffer_size);
    *responses = responses_init;

    return true;
}

bool Relay_Proxy_PrecompiledResp_Dynamic_AddQuickResp(tag_quick_response_info_t *responses_init, uint8_t *data){
    responses_init->flags = data[0];
    responses_init->query_n = data[1];
    responses_init->response_n = data[2];
    responses_init->fix_n = data[3];
    responses_init->x_query_n = (responses_init->query_n*2/8);
    if(((responses_init->query_n*2)%8) != 0) responses_init->x_query_n++;

    responses_init->query = BigBuf_calloc(responses_init->query_n);
    memcpy(responses_init->query, data+4, responses_init->query_n);
    Dbprintf("(quick_resp data): %s", hex_text(data, 4+responses_init->query_n+responses_init->response_n+responses_init->x_query_n));
    Dbprintf("(quick_resp ADD ): %s", hex_text(responses_init->query, responses_init->query_n));
    
    responses_init->response = BigBuf_calloc(responses_init->response_n+2); // + if needed for crc.
    memcpy(responses_init->response, data+4+responses_init->query_n, responses_init->response_n);
    responses_init->x_query = BigBuf_calloc(responses_init->x_query_n);
    memcpy(responses_init->x_query, data+4+responses_init->query_n+responses_init->response_n, responses_init->x_query_n);
    
    if((responses_init->flags & 2) == 2){ // ADD CRC
        AddCrc14A(responses_init->response, responses_init->response_n);
        responses_init->response_n += 2;
    }
    Dbprintf("(quick_resp ADD2): %s", hex_text(responses_init->query, responses_init->query_n));
    return addQuickReply(responses_init);
}

bool Relay_Proxy_PrecompiledResp_ATS(iso14a_card_select_t *card, tag_quick_response_info_t *responses_init){
    int num_responses = 0;
    int sum_bytes = 0;
    //
    sum_bytes += card->ats_len+2;
    num_responses++;
    uint8_t* ATS = BigBuf_calloc(card->ats_len+2);
    for(int i = 0; i< card->ats_len; i++){
        ATS[i] = card->ats[i];
    }
    AddCrc14A(ATS, card->ats_len);

    responses_init->response = ATS;
    responses_init->response_n = card->ats_len+2;
    responses_init->query = BigBuf_calloc(1);
    responses_init->query[0] = ISO14443A_CMD_RATS;
    responses_init->query_n = 1;
    responses_init->flags = 1; // startsWith()
    responses_init->fix_n = 4;
    responses_init->x_query_n = (responses_init->query_n*2/8);
    if(((responses_init->query_n*2)%8) != 0) responses_init->x_query_n++;
    responses_init->x_query = BigBuf_calloc(responses_init->x_query_n);

    return addQuickReply(responses_init);
}

bool addQuickReply(tag_quick_response_info_t *responses_init){
    size_t alloc_tag_modulation_buffer_size = responses_init->response_n * 9 + 3; // (all bytes)

    uint8_t *free_buffer = BigBuf_calloc(alloc_tag_modulation_buffer_size);
    uint8_t *free_buffer_pointer = free_buffer;
    if (prepare_allocated_tag_modulation((tag_response_info_t*)responses_init, &free_buffer_pointer, &alloc_tag_modulation_buffer_size) == false) {
        BigBuf_free_keep_EM();
        Dbprintf("Not enough modulation buffer size query starts: %02X", responses_init->query[0]);
        return false;
    }
    return true;
}

bool Relay_Proxy_SendResponse(uint8_t *data, uint16_t len, bool addCRC){
    uint8_t dynamic_response_buffer[260] = {0}; // 64
    uint8_t dynamic_modulation_buffer[2350] = {0}; //512
    tag_response_info_t dynamic_response_info = {
        .response = dynamic_response_buffer,
        .response_n = 0,
        .modulation = dynamic_modulation_buffer,
        .modulation_n = 0
    };

    for(int i = 0; i < len; i++){
        dynamic_response_info.response[i] = data[i];
    }
    dynamic_response_info.response_n = len;

    // Add CRC bytes, always used in ISO 14443A-4 compliant cards
    if(addCRC){
        AddCrc14A(dynamic_response_info.response, dynamic_response_info.response_n);
        dynamic_response_info.response_n += 2;
    }

    if (prepare_tag_modulation(&dynamic_response_info, sizeof(dynamic_modulation_buffer)) == false) {
        DbpString("Error preparing tag response");
        Dbhexdump(dynamic_response_info.response_n, dynamic_response_info.response, false);
        return false;
    }

    // Send response
    EmSendPrecompiledCmd(&dynamic_response_info);
    //Dbprintf("rn %u   mn %u", dynamic_response_info.response_n, dynamic_response_info.modulation_n);
    memcpy(prev_resp, dynamic_response_info.response, dynamic_response_info.response_n);
    prev_resp_len = dynamic_response_info.response_n;
    // LogTrace(dynamic_response_info.response, dynamic_response_info.response_n, 0, 0, NULL, false);
    return true;
}

static void SetFSD(uint8_t fsdi){
    switch (fsdi) {
        case 0:
            fsd = 16;
        break;
        case 1:
            fsd = 24;
        break;
        case 2:
            fsd = 32;
        break;
        case 3:
            fsd = 40;
        break;
        case 4:
            fsd = 48;
        break;
        case 5:
            fsd = 64;
        break;
        case 6:
            fsd = 96;
        break;
        case 7:
            fsd = 128;
        break;
        case 8:
            fsd = 256;
        break;
        case 9:
            fsd = 512;
        break;
        case 10:
            fsd = 1024;
        break;
        case 11:
            fsd = 2048;
        break;
        default:
            fsd = 4096;
        break;
    }
}

int Proxy_AutoResponse(tag_response_info_t *responses, tag_quick_response_info_t *responses2, iso14a_card_select_t *card, uint8_t *receivedCmd, int len) {
    if(tag_type_14443_4 && is_13334_4_WTX(receivedCmd, len)){ // WTX resp
        Dbprintf("S(WTX) Response ... no reply");
        return -10;
    }

    if(chainingBufLen > 0){ // chaining
        if((receivedCmd[0] == 0xB3 && waiting_block_num_02) || (receivedCmd[0] == 0xB2 && waiting_block_num_02 == false) || is_13334_4_Deselect(receivedCmd, len)){ // send again
            chainingBufLen = 0;
            Dbprintf("Chaining %02X  ERROR", receivedCmd[0]);
        }else if((receivedCmd[0] & 0xA2) == 0xA2){ // ACK
            return DoChaining();
        }else{
            Dbprintf("Chaining rec:%02X... ERROR", receivedCmd[0]);
            chainingBufLen = 0;
        }
    }
    
    tag_response_info_t *p_response = NULL;
    if (receivedCmd[0] == ISO14443A_CMD_REQA && len == 1) {
        p_response = &responses[RESP_INDEX_ATQA];
    } else if (receivedCmd[0] == ISO14443A_CMD_WUPA && len == 1) {
        p_response = &responses[RESP_INDEX_ATQA];
    } else if (receivedCmd[1] == 0x20 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT && len == 2) {    // Received request for UID (cascade 1)
        p_response = &responses[RESP_INDEX_UIDC1];
    } else if (receivedCmd[1] == 0x20 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT_2 && len == 2) {  // Received request for UID (cascade 2)
        p_response = &responses[RESP_INDEX_UIDC2];
    } else if (receivedCmd[1] == 0x20 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT_3 && len == 2) {  // Received request for UID (cascade 3)
        p_response = &responses[RESP_INDEX_UIDC3];
    } else if (receivedCmd[1] == 0x70 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT && len == 9) {    // Received a SELECT (cascade 1)
        p_response = &responses[RESP_INDEX_SAKC1];
    } else if (receivedCmd[1] == 0x70 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT_2 && len == 9) {  // Received a SELECT (cascade 2)
        p_response = &responses[RESP_INDEX_SAKC2];
    } else if (receivedCmd[1] == 0x70 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT_3 && len == 9) {  // Received a SELECT (cascade 3)
        p_response = &responses[RESP_INDEX_SAKC3];
    } else if (receivedCmd[0] == ISO14443A_CMD_HALT && len == 4) {    // Received a HALT
        prev_resp_len = 0;
        return -10;
    } else if (receivedCmd[0] == ISO14443A_CMD_PPS && len == 5) {    // Received a PPS
        p_response = &responses[RESP_INDEX_PPS];

    } else if (receivedCmd[0] == ISO14443A_CMD_RATS && len == 4) {    // Received a RATS request
        // Dbprintf("0xE0");
        SetFSD(receivedCmd[1] & 0xF0);
        if (card->ats_len == 0) {    // RATS not supported
            EmSend4bit(CARD_NACK_NA);
            prev_resp[0] = CARD_NACK_NA; 
            prev_resp_len = 1;
            return 1;
        } else {
            // Find last added ATS... (if we use diffrent tags in one sesion.)
            for(int i = count_quick_resp-1; i >= 0; i--){
                if(responses2[i].query_n != 0 && responses2[i].query[0] == ISO14443A_CMD_RATS){
                    p_response = (tag_response_info_t*)&responses2[i];
                    Dbprintf("0xE0 p_r %p", p_response);
                    break;
                }
            }
        } 
    } 

    if(diff_time_test && p_response != NULL){
        Dbprintf("Testing time Failed... now:%u ms   waiting:%u ms", (GetCountUS()-start_time_test)/1000, diff_time_test/1000);
        diff_time_test = 0; 
    }

    if(p_response == NULL){ // QUICK RESPONSE
        for(int i = 0; i < count_quick_resp; i++){
            if(responses2[i].fix_n > 0 && responses2[i].fix_n != len){ continue; }
            if(responses2[i].query_n > len){ continue; }
            if((responses2[i].flags & 1) == 0 && responses2[i].query_n != len){ continue; }

            bool eq = false;
            for(int j = 0; j < responses2[i].query_n; j++){ // match tag_rec bytes.
                bool x_bit_L = (responses2[i].x_query[(j*2)/8] & (1 << ((j*2)%8) )); // X0
                bool x_bit_R = (responses2[i].x_query[((j*2)+1)/8] & (1 << (((j*2)+1)%8) )); // 0X 
                uint8_t in = receivedCmd[j];
                if(x_bit_L){
                    in &= 0x0F;
                }
                if(x_bit_R){
                    in &= 0xF0;
                }
                Dbprintf("q r: %02X  %02X    x_L:%u  x_R:%u", receivedCmd[j], responses2[i].query[j], x_bit_L, x_bit_R);
                if(in == responses2[i].query[j]){ // EQ
                    eq = true;
                }else{
                    Dbprintf("q r B:%d   %02X != %02X    x_L:%u  x_R:%u", j, receivedCmd[j], responses2[i].query[j], x_bit_L, x_bit_R);
                    eq = false;
                    break;
                }
            }
            if(eq){
                p_response = (tag_response_info_t*)&responses2[i];
                Dbprintf("(quick_resp) From reader : %s", hex_text(receivedCmd, len));
                Dbprintf("(quick_resp) Sending back: %s", hex_text(responses2[i].response, responses2[i].response_n));
            }
        }
    }

    if(p_response == NULL){
        // Allocate 512 bytes for the dynamic modulation, created when the reader queries for it
        // Such a response is less time critical, so we can prepare them on the fly
        bool addCRC = true;
        uint8_t dynamic_response_buffer[260] = {0};
        uint8_t dynamic_modulation_buffer[2350] = {0};
        tag_response_info_t dynamic_response_info = {
            .response = dynamic_response_buffer,
            .response_n = 0,
            .modulation = dynamic_modulation_buffer,
            .modulation_n = 0
        };
        if (is_13334_4_Deselect(receivedCmd, len)){
            dynamic_response_info.response[0] = receivedCmd[0];
            dynamic_response_info.response_n = 1;
            Dbprintf("%02X (S-block DESELECT). Sending back C2.", receivedCmd[0]);
            waitingForMole = false;
            if(test_time && diff_time_test){
                Dbprintf("Testing time Failed... now:%u ms   waiting:%u ms", (GetCountUS()-start_time_test)/1000, diff_time_test/1000);
                diff_time_test = 0; 
            }
        } else if (receivedCmd[0] == 0xF0 && len < 200 && receivedCmd[1] == (uint8_t)(len-3)) { // Android P2P // receivedCmd[1] == (uint8_t)(len+3)
            Dbprintf("IGNORE F0 !!! %u  %02X  %02X", receivedCmd[1] == (uint8_t)(len-3), receivedCmd[1], (uint8_t)(len-3));
            Dbprintf("IGNORE %s", hex_text(receivedCmd, len));
            return -10;
        } else if((receivedCmd[0] == 0xB2 || receivedCmd[0] == 0xB3) && len == 3){
            dynamic_response_info.response_n = 1;
            dynamic_response_info.response[0] = (receivedCmd[0]==0xB2 ? 0xA3 : 0xA2);
            if(prev_cmd[0] == ISO14443A_CMD_RATS){
                
            }else if(  (receivedCmd[0] == 0xB3 && waiting_block_num_02)
                    || (receivedCmd[0] == 0xB2 && waiting_block_num_02 == false)){
                
                if(waitingForMole) {
                    dynamic_response_info.response[0] = 0xF2;
                    dynamic_response_info.response[1] = 0x05;
                    dynamic_response_info.response_n = 2;
                    Dbprintf("Waiting For Mole !!");
                }
            } else{

                if(waitingForMole) {
                    Dbprintf("Waiting For Mole !!!");
                    dynamic_response_info.response[0] = 0xF2;
                    dynamic_response_info.response[1] = 0x05;
                    dynamic_response_info.response_n = 2;
                }else if(test_time != NULL && (GetCountUS()-start_time_test) < test_time->ms * 1000){
                    // flags wtxm 4, dont 8, error 16, rfu1 32, rfu2 64, rfu3 128
                    if(test_time->flags & 4){
                        dynamic_response_info.response[0] = 0xF2;
                        dynamic_response_info.response[1] = test_time->wtxm;
                        dynamic_response_info.response_n = 2;
                    }else if(test_time->flags & 8){
                        prev_resp_len = 0;
                        return -10; 
                        Dbprintf("(test_time): DONT reply");
                    }else if(test_time->flags & 16){
                        dynamic_response_info.response[0] = 0x02;
                        dynamic_response_info.response[1] = 0x11;
                        dynamic_response_info.response[2] = 0x22;
                        dynamic_response_info.response[3] = 0x33; // this is CRC error 
                        dynamic_response_info.response_n = 4;
                        Dbprintf("(test_time): Sending ERROR");   
                    }else if(test_time->flags & 32){ 
                        Dbprintf("(test_time): Sending R(ACK)");   
                    }

                }else{
                    memcpy(dynamic_response_info.response, prev_resp, prev_resp_len-2);
                    dynamic_response_info.response_n = prev_resp_len-2;
                    Dbprintf("%02X (R-block NACK). Sending previous reply", receivedCmd[0]);
                }
            }

            // memcpy(dynamic_response_info.response, prev_resp, prev_resp_len-2);
            // dynamic_response_info.response_n = prev_resp_len-2;
            // DbpString("B2 or B3 (R-block NACK). Sending previous reply");
            Dbprintf("%02X (R-block NACK) ... resp:%02X %02X ...   prev_cmd %02X   len: %u", receivedCmd[0], dynamic_response_info.response[0], dynamic_response_info.response[1], prev_resp[0], prev_resp_len);
        }
        
        if (dynamic_response_info.response_n > 0) {
            // Add CRC bytes, always used in ISO 14443A-4 compliant cards
            if(addCRC){
                AddCrc14A(dynamic_response_info.response, dynamic_response_info.response_n);
                dynamic_response_info.response_n += 2;
            }

            if (prepare_tag_modulation(&dynamic_response_info, 2350) == false) {
                DbpString("Error preparing tag response.");
                Dbhexdump(dynamic_response_info.response_n, dynamic_response_info.response, false);
                return -1;
            }
            p_response = &dynamic_response_info;
        }
        if(p_response == NULL){
            LED_B_ON();
            LED_C_ON();
            return 0;
        }
    }
    // Send response
    EmSendPrecompiledCmd(p_response);
    
    if(receivedCmd[0] == 0xB2 || receivedCmd[0] == 0xB3){ // store prev cmd and respond.
        // do nothing.
    }else{
        memcpy(prev_resp, p_response->response, p_response->response_n);
        prev_resp_len = p_response->response_n;
    }
    // Dbprintf("p %02X   n%u  len%d ", p_response->response[0], p_response->response_n, len);
    return (int)p_response->response_n;
}

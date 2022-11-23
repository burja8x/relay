#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "relay.h"
#include "log.h"
#include "pipe.h"
#include "ws.h"

static uint8_t new_UID[10] = {0};
static int new_UID_len = 0;
static uint8_t new_ATQA[4] = {0};
static bool new_ATQA_set = false;
static uint8_t new_SAK = 0;
static int new_SAK_set = false;
static uint8_t new_ATS[256] = {0};
static int new_ATS_len = 0;
static uint8_t new_FWI = 0;
static bool new_FWI_set = false;
static uint8_t new_SFGI = 0;
static bool new_SFGI_set = false;
static uint8_t new_TA1 = 0;
static bool new_TA1_set = false;

static uint8_t new_card_info[sizeof(iso14a_card_select_t)+5] = {0};

static uint8_t *arr_send_later[100] = {NULL};
static uint8_t arr_send_later_len = 0;

// For parsing string
static int getValue(int length, char* data, char* what, uint8_t* where){
    char p[] = "0x00";
    char *pfound = strstr(data, what);
    if (pfound != NULL){
        int dposfound = pfound - data;
        printf("getValue(%s) pos:%d\n", what, dposfound);
        int len = -10000;
        int lenB = -10000;
        for(int i = dposfound; i < length; i++){
            if(data[i] == '|'){
                printf("getValue(%s) len:%d\n", what, lenB);
                return lenB;
            }else if(data[i] == ':'){
                len = 0;
                lenB = 0;
                printf("0\n");
                // *where = data + i + 1;
            }else{
                if(lenB < 0){
                    continue;
                }
                printf("len:%d  %c\n", len, data[i]);
                if(len%2 == 0){
                    p[2] = data[i];
                }else{
                    p[3] = data[i];
                    unsigned int intVal;
                    sscanf(p, "%x", &intVal);
                    where[lenB] = (uint8_t)intVal;
                    // printf("len :%d  %c\n", len, data[i]);
                    lenB++;
                }
                len++;
            }
        } 
    }
    return -1;
}

// For parsing string
static int getValue2(int length, char* data, char* what, uint8_t* where){
    char p[] = "0x00";
    char *pfound = strstr(data, what);
    if (pfound != NULL){
        printf("getValue(%s)\n", what);
        int dposfound = pfound - data;
        int len = -10000;
        int lenB = -10000;
        for(int i = dposfound; i < length; i++){
            if(data[i] == '|'){
                return len;
            }else if(data[i] == ':'){
                len = 0;
                p[2] = data[i+1];
                p[3] = data[i+2];
                unsigned int intVal;
                sscanf(p, "%x", &intVal);
                *where = (uint8_t)intVal;
                printf("getValue(%s) len:%d data:%u\n", what, lenB, intVal);
                return 1;
            }else{
                len++;
            }
        } 
    }
    return -1;
}

// Parse settings
void parseStartRelay(int length, char* data){
    new_UID_len = 0;
    new_ATQA_set = false;
    new_SAK_set = false;
    new_ATS_len = 0;
    new_FWI_set = false;
    new_TA1_set = false;
    new_SFGI_set = false;
    for(int i = 0; i < arr_send_later_len; i++){
        if(arr_send_later[i]){
            free(arr_send_later[i]);
            arr_send_later[i] = NULL;
        }
    }
    arr_send_later_len = 0;

    int len = getValue(length, data, "UID:", new_UID);
    if(len > 0){
        new_UID_len = len;
    }
    len = getValue(length, data, "ATQA:", new_ATQA);
    if(len > 0){
        new_ATQA_set = true;
    }
    len = getValue(length, data, "ATS:", new_ATS);
    if(len > 0){
        new_ATS_len = len;
    }

    len = getValue2(length, data, "SAK:", &new_SAK);
    if(len > 0){
        new_SAK_set = true;
    }
    len = getValue2(length, data, "FWI:", &new_FWI);
    if(len > 0){
        new_FWI_set = true;
    }
    len = getValue2(length, data, "SFGI:", &new_SFGI);
    if(len > 0){
        new_SFGI_set = true;
    }
    len = getValue2(length, data, "TA1:", &new_TA1);
    if(len > 0 && new_TA1 > 0){
        new_TA1_set = true;
    }

    // CHANGE BYTES
    char p[] = "0x00";
    for(int i = 0; i < length; i++){
        char *pfound = strstr(data, "change"); // "change$112233$TR$00AA11$Y$N$AAADD012|
        if (pfound != NULL){
            relay_change_data rcd = {};
            uint8_t* rcd_data = calloc(400, sizeof(uint8_t));
            uint8_t* what = rcd_data;
            uint8_t* replace_with = (rcd_data+100);
            uint8_t* x_what = (rcd_data+200);
            uint8_t* x_replace_with = (rcd_data+230);
            uint8_t* x_prev = (rcd_data+260);
            uint8_t* prev = (rcd_data+290);
            bool err = false;
            int dollar_count = 0;
            int len = -1000;
            Log(TRACE, "(change) %s", pfound);
            int dposfound = pfound - data;
            for(int i = dposfound; i < length; i++){
                if(data[i] == '|'){
                    data = &data[i]; // AT THE END.
                    break;
                }else if(data[i] == '$'){
                    len = 0;
                    dollar_count++;
                }else if(data[i] == 'T'){
                    if(dollar_count == 2){ // tag
                        rcd.tag_prev |= 2;
                    }else{
                        err = true; break;
                    }
                }else if(data[i] == 'Y'){
                    if(dollar_count == 4){ // true
                        rcd.addCRC_setBN |= 2;
                    }else if(dollar_count == 5){ // true
                        rcd.addCRC_setBN |= 1;
                    }else{
                        err = true; break;
                    }
                }else if(data[i] == 'P'){ // prev 1st
                    if(dollar_count == 2){
                        rcd.tag_prev |= 1;
                    }else{
                        err = true; break;
                    }
                }else if(data[i] == 'N'){
                    // if(dollar_count == 4){ // false
                    //     rcd.addCRC_setBN &= 1;
                    // }else if(dollar_count == 5){ // false
                    //     rcd.addCRC_setBN &= 2;
                    // }else{
                    //     err = true; break;
                    // }
                }else if(data[i] == 'X'){ // X
                    if(dollar_count == 1){
                        x_what[len/8] |= 1 << (len%8); // set bit in array (at Len position).
                    }else if(dollar_count == 3){
                        x_replace_with[len/8] |= 1 << (len%8);
                    }else if(dollar_count == 6){
                        x_prev[len/8] |= 1 << (len%8);
                    }else{
                        err = true; break;
                    }
                    data[i] = '0';
                    goto x_con;
                }else{
                    if(dollar_count == 0){
                        continue; 
                    }
x_con:              if(dollar_count == 1 || dollar_count == 3 || dollar_count == 6){
                        if(len%2 == 0){ // HEX
                            p[2] = data[i];
                        }else{
                            p[3] = data[i];
                            unsigned int intVal;
                            sscanf(p, "%x", &intVal);

                            if(dollar_count == 1){
                                what[len/2] = (uint8_t)intVal;
                                rcd.what_len++;
                            }else if(dollar_count == 3){
                                replace_with[len/2] = (uint8_t)intVal;
                                rcd.replace_with_len++;
                            }else if(dollar_count == 6){
                                prev[len/2] = (uint8_t)intVal;
                                rcd.prev_len++;
                            }else{
                                err = true; break;
                            }
                        }
                    }else{
                        err = true; break;
                    }
                    len++;
                }
            }
            if(err){
                Log(ERROR ,"Error at: parseStartRelay() dollar count:%d  len:%d  '%s'", dollar_count, len, pfound);
                data = pfound + 5;
            }else{
                // SEND to pm3.
                int x_what_len = (rcd.what_len*2/8);
                if(((rcd.what_len*2)%8) != 0) x_what_len++;

                int x_replace_with_len = (rcd.replace_with_len*2/8);
                if(((rcd.replace_with_len*2)%8) != 0) x_replace_with_len++;

                int x_prev_len = (rcd.prev_len*2/8);
                if(((rcd.prev_len*2)%8) != 0) x_prev_len++;

                int packet_size = sizeof(relay_change_data) + rcd.what_len + rcd.replace_with_len
                                + rcd.prev_len + x_what_len + x_replace_with_len + x_prev_len + 5;
                uint8_t* send_data = calloc(packet_size, sizeof(uint8_t));
                send_data[0] = ((packet_size-3) >> 0) & 0xFF;
                send_data[1] = ((packet_size-3) >> 8) & 0xFF;
                send_data[2] = P_RELAY;
                send_data[3] = RELAY_SET_CHANGE;
                uint8_t* d = send_data + 5;
                memcpy(d, &rcd, sizeof(relay_change_data));
                d += sizeof(relay_change_data);
                memcpy(d, what, rcd.what_len);
                d += rcd.what_len;
                memcpy(d, replace_with, rcd.replace_with_len);
                d += rcd.replace_with_len;
                memcpy(d, prev, rcd.prev_len);
                d += rcd.prev_len;
                memcpy(d, x_what, x_what_len);
                d += x_what_len;
                memcpy(d, x_replace_with, x_replace_with_len);
                d += x_replace_with_len;
                memcpy(d, x_prev, x_prev_len);
                // d += x_prev_len;
                Log(TRACE, "change data: %s", hex_text(send_data, packet_size));
                Log(TRACE, "x_what: %s", hex_text(x_what, x_what_len));
                Log(TRACE, "x_replace_with: %s", hex_text(x_replace_with, x_replace_with_len));
                Log(TRACE, "x_prev: %s", hex_text(x_prev, x_prev_len));
                Log(TRACE, "change: %u  %u  %u  %u", rcd.what_len, rcd.replace_with_len, rcd.prev_len, sizeof(relay_change_data));
                Log(TRACE, "      : %u  %u  %u", x_what_len, x_replace_with_len, x_prev_len);

                arr_send_later[arr_send_later_len] = send_data;
                arr_send_later_len++;
            }
            free(rcd_data);
        }
    }

    // INSERT
    for(int i = 0; i < length; i++){
        char *pfound = strstr(data, "insert"); // insert$1111$1122$AAAA$ST|
        if (pfound != NULL){
            relay_insert_data rid = {};
            uint8_t* i_data = calloc(360, sizeof(uint8_t));
            uint8_t* send = i_data;
            uint8_t* tag_rec = (i_data+100);
            uint8_t* tag_resp = (i_data+200);
            uint8_t* x_tag_rec = (i_data+300);
            uint8_t* x_tag_resp = (i_data+330);
            
            bool err = false;
            int dollar_count = 0;
            int len = -1000;
            Log(TRACE, "(insert) %s", pfound);
            int dposfound = pfound - data;
            for(int i = dposfound; i < length; i++){
                if(data[i] == '|'){
                    data = &data[i]; // AT THE END.
                    break;
                }else if(data[i] == '$'){
                    len = 0;
                    dollar_count++;
                }else if(data[i] == 'T'){ // add CRC
                    if(dollar_count == 4){
                        rid.flags |= 2;
                    }else{
                        err = true; break;
                    }
                }else if(data[i] == 'S'){ // set block number
                    if(dollar_count == 4){
                        rid.flags |= 1;
                    }else{
                        err = true; break;
                    }
                }else if(data[i] == 'X'){ // X
                    if(dollar_count == 2){
                        x_tag_rec[len/8] |= 1 << (len%8); // set bit in array (at Len position).
                    }else if(dollar_count == 3){
                        x_tag_resp[len/8] |= 1 << (len%8);
                    }else{
                        err = true; break;
                    }
                    data[i] = '0';
                    goto x_con_i;
                }else{
                    if(dollar_count == 0){
                        continue; 
                    }
x_con_i:            if(dollar_count == 1 || dollar_count == 2 || dollar_count == 3){
                        if(len%2 == 0){ // HEX
                            p[2] = data[i];
                        }else{
                            p[3] = data[i];
                            unsigned int intVal;
                            sscanf(p, "%x", &intVal);

                            if(dollar_count == 1){
                                send[len/2] = (uint8_t)intVal;
                                rid.send_len++;
                            }else if(dollar_count == 2){
                                tag_rec[len/2] = (uint8_t)intVal;
                                rid.tag_rec_len++;
                            }else if(dollar_count == 3){
                                tag_resp[len/2] = (uint8_t)intVal;
                                rid.tag_resp_len++;
                            }else{
                                err = true; break;
                            }
                        }
                    }else{
                        err = true; break;
                    }
                    len++;
                }
            }
            if(err){
                Log(ERROR ,"Error at: parseStartRelay(insert) dollar count:%d  len:%d  '%s'", dollar_count, len, pfound);
                data = pfound + 5;
            }else{
                // SEND to pm3.
                int x_tag_rec_len = (rid.tag_rec_len*2/8);
                if(((rid.tag_rec_len*2)%8) != 0) x_tag_rec_len++;

                int x_tag_resp_len = (rid.tag_resp_len*2/8);
                if(((rid.tag_resp_len*2)%8) != 0) x_tag_resp_len++;

                int packet_size = sizeof(relay_insert_data) + rid.send_len + rid.tag_rec_len
                                + rid.tag_resp_len + x_tag_rec_len + x_tag_resp_len + 5;
                uint8_t* send_data = calloc(packet_size, sizeof(uint8_t));
                send_data[0] = ((packet_size-3) >> 0) & 0xFF;
                send_data[1] = ((packet_size-3) >> 8) & 0xFF;
                send_data[2] = P_RELAY;
                send_data[3] = RELAY_SET_INSERT;
                uint8_t* d = send_data + 5;
                memcpy(d, &rid, sizeof(relay_insert_data));
                d += sizeof(relay_insert_data);
                memcpy(d, send, rid.send_len);
                d += rid.send_len;
                memcpy(d, tag_rec, rid.tag_rec_len);
                d += rid.tag_rec_len;
                memcpy(d, tag_resp, rid.tag_resp_len);
                d += rid.tag_resp_len;
                memcpy(d, x_tag_rec, x_tag_rec_len);
                d += x_tag_rec_len;
                memcpy(d, x_tag_resp, x_tag_resp_len);
                
                Log(TRACE, "insert data: %s", hex_text(send_data, packet_size));
                Log(TRACE, "x_tag_rec: %s", hex_text(x_tag_rec, x_tag_rec_len));
                Log(TRACE, "x_tag_resp: %s", hex_text(x_tag_resp, x_tag_resp_len));
                Log(TRACE, "insert: %u  %u  %u  %u", rid.send_len, rid.tag_rec_len, rid.tag_resp_len, sizeof(relay_insert_data));
                Log(TRACE, "      : %u  %u", x_tag_rec_len, x_tag_resp_len);

                arr_send_later[arr_send_later_len] = send_data;
                arr_send_later_len++;
            }
            free(i_data);
        }
    }

    // QUICK RESPONSE
    for(int i = 0; i < length; i++){ 
        char n[] = "   ";
        uint8_t dollar_count = 0;
        bool err = false;
        char *pfound = strstr(data, "quick"); // "quick$112233$00AA11$TS$10|
        if (pfound != NULL){
            int len = -1000;
            Log(TRACE, "(quick) %s", pfound);
            int dposfound = pfound - data;
            uint8_t* q_data = calloc(220, sizeof(uint8_t));
            uint8_t* what = q_data;
            uint8_t* resp = (q_data+100);
            uint8_t quickFlags = 0;
            uint8_t what_len = 0;
            uint8_t resp_len = 0;
            uint8_t fix_len = 0;
            uint8_t* x_what = (q_data+200);
            for(int i = dposfound; i < length; i++){
                if(data[i] == '|'){
                    unsigned int intVal;
                    sscanf(n, "%u", &intVal);
                    fix_len = (uint8_t)intVal;
                    data = &data[i]; // AT THE END.
                    break;
                }else if(data[i] == '$'){
                    len = 0;
                    dollar_count++;
                }else if(data[i] == 'X'){
                    if(dollar_count == 1){
                        x_what[len/8] |= 1 << (len%8); // set bit in array (at Len position).
                    }else{
                        err = true; break;
                    }
                    data[i] = '0';
                    goto x_con_q;
                }else if(data[i] == 'S'){ // starts with flag
                    if(dollar_count == 3){
                        quickFlags |= 1;
                    }else{
                        err = true; break;
                    }
                }else if(data[i] == 'T'){ // add CRC
                    if(dollar_count == 3){
                        quickFlags |= 2;
                    }else{
                        err = true; break;
                    }
                }else{
                    if(dollar_count == 0){
                        continue; 
                    }
x_con_q:            if(dollar_count == 1 || dollar_count == 2){
                        if(len%2 == 0){ // HEX
                            p[2] = data[i];
                        }else{
                            p[3] = data[i];
                            unsigned int intVal;
                            sscanf(p, "%x", &intVal);

                            if(dollar_count == 1){
                                what[len/2] = (uint8_t)intVal;
                                what_len++;
                            }else if(dollar_count == 2){
                                resp[len/2] = (uint8_t)intVal;
                                resp_len++;
                            }else{
                                err = true; break;
                            }
                        }
                    }else if(dollar_count == 4){
                        n[len] = data[i];
                    }else{
                        err = true; break;
                    }
                    len++;
                }
            }
            if(err){
                Log(ERROR ,"Error at: parseStartRelay(quick) dollar_count:%d  len:%d  '%s'", dollar_count, len, pfound);
                data = pfound + 5;
            }else{
                int x_what_len = (what_len*2/8);
                if(((what_len*2)%8) != 0) x_what_len++;

                int packet_size = 5 + what_len + resp_len + 1+1+1+1 + x_what_len;
                uint8_t* send_data = calloc(packet_size, sizeof(uint8_t));
                send_data[0] = ((packet_size-3) >> 0) & 0xFF;
                send_data[1] = ((packet_size-3) >> 8) & 0xFF;
                send_data[2] = P_RELAY;
                send_data[3] = RELAY_SET_QUICK_REPLY;
                send_data[5] = quickFlags;
                send_data[6] = what_len;
                send_data[7] = resp_len;
                send_data[8] = fix_len;
                uint8_t* d = send_data + 5 + 4;
                memcpy(d, what, what_len);
                d += what_len;
                memcpy(d, resp, resp_len);
                d += resp_len;
                memcpy(d, x_what, x_what_len);

                Log(TRACE, "(quick) change data: %s", hex_text(send_data, packet_size));
                Log(TRACE, "x_what: %s", hex_text(x_what, x_what_len));
                arr_send_later[arr_send_later_len] = send_data;
                arr_send_later_len++;
            }
            free(q_data);
        }
    }

    // TEST TIME
    for(int i = 0; i < length; i++){
        char n[] = "      ";
        uint8_t dollar_count = 0;
        bool err = false;
        relay_time time = {};
        uint8_t *bytes = calloc(50, sizeof(uint8_t)); 
        uint8_t *x_bytes = calloc(10, sizeof(uint8_t)); 
        char *pfound = strstr(data, "time"); // "time$344$SI$59$AAXX00|
        if (pfound != NULL){
            int len = -1000;
            Log(TRACE, "(time) %s", pfound);
            int dposfound = pfound - data;

            for(int i = dposfound; i < length; i++){
                if(data[i] == '|'){
                    data = &data[i]; // AT THE END.
                    break;
                }else if(data[i] == '$'){
                    if(dollar_count == 1){
                        unsigned int intVal;
                        sscanf(n, "%u", &intVal);
                        time.ms = intVal;
                    }
                    if(dollar_count == 3){
                        unsigned int intVal;
                        sscanf(n, "%u", &intVal);
                        time.wtxm = intVal;
                    }
                    // else{
                    //     err = true; break;
                    // }
                    memset(n, ' ', 6);
                    len = 0;
                    dollar_count++;
                }else if(data[i] == 'S'){ // starts with
                    if(dollar_count == 2){time.flags |= 1;}else{err = true; break;}
                }else if(data[i] == 'I'){ // only I-block
                    if(dollar_count == 2){time.flags |= 2;}else{err = true; break;}
                }else if(data[i] == 'W'){ // wtxm
                    if(dollar_count == 2){time.flags |= 4;}else{err = true; break;}
                }else if(data[i] == 'P'){ // dont reply
                    if(dollar_count == 2){time.flags |= 8;}else{err = true; break;}
                }else if(data[i] == 'L'){ // send error
                    if(dollar_count == 2){time.flags |= 16;}else{err = true; break;}
                }else if(data[i] == 'R'){ // rfu 1
                    if(dollar_count == 2){time.flags |= 32;}else{err = true; break;}
                }else if(data[i] == 'T'){ // rfu 2
                    if(dollar_count == 2){time.flags |= 64;}else{err = true; break;}
                }else if(data[i] == 'Y'){ // rfu 3
                    if(dollar_count == 2){time.flags |= 128;}else{err = true; break;}
                }else if(data[i] == 'X'){
                    if(dollar_count == 4){
                        x_bytes[len/8] |= 1 << (len%8); // set bit in array (at Len position).
                    }else{
                        err = true; break;
                    }
                    data[i] = '0';
                    goto x_con_t;
                }else{
x_con_t:            if(dollar_count == 0){
                        continue;
                    }
                    if(dollar_count == 1 || dollar_count == 3){
                        if(len > 6){
                            err = true; break;
                        }
                        n[len] = data[i];
                    }else if(dollar_count == 4){
                        if(len%2 == 0){ // HEX
                            p[2] = data[i];
                        }else{
                            p[3] = data[i];
                            unsigned int intVal;
                            sscanf(p, "%x", &intVal);
                            bytes[len/2] = (uint8_t)intVal;
                            time.bytes_len++;
                        }
                    }else{
                        err = true; break;
                    }
                    len++;
                }
            }
            if(err){
                Log(ERROR ,"Error at: parseStartRelay(time) dollar_count:%d  len:%d  '%s'", dollar_count, len, pfound);
                data = pfound + 5;
            }else{
                int x_bytes_len = (time.bytes_len*2/8);
                if(((time.bytes_len*2)%8) != 0) x_bytes_len++;

                int packet_size = 5 + sizeof(relay_time) + time.bytes_len + x_bytes_len;
                uint8_t* send_data = calloc(packet_size, sizeof(uint8_t));
                send_data[0] = ((packet_size-3) >> 0) & 0xFF;
                send_data[1] = ((packet_size-3) >> 8) & 0xFF;
                send_data[2] = P_RELAY;
                send_data[3] = RELAY_SET_TEST_TIME;
                uint8_t* d = send_data + 5;
                memcpy(d, &time, sizeof(relay_time));
                d += sizeof(relay_time);
                memcpy(d, bytes, time.bytes_len);
                d += time.bytes_len;
                memcpy(d, x_bytes, x_bytes_len);
                Log(TRACE, "(time) %u ms | flags: %u | wtxm: %u | data: %s", time.ms, time.flags, time.wtxm, hex_text(send_data, packet_size));
                
                arr_send_later[arr_send_later_len] = send_data;
                arr_send_later_len++;
            }
        }
        free(bytes);
        free(x_bytes);
    }
}

// Set CARD info... change UID, ATQA, ATS, SAK, FWI, SFGI, TA1
uint8_t* setNewValues(uint8_t* in_mole_card){
    memset(new_card_info, 0, sizeof(new_card_info));

    uint16_t msgLen = in_mole_card[0] | (in_mole_card[1] << 8);
    memcpy(new_card_info, in_mole_card, msgLen+3); // copy entire message.

    iso14a_card_select_t* mole_card = (iso14a_card_select_t*)(in_mole_card+5);
    iso14a_card_select_t* new_card = (iso14a_card_select_t*)(new_card_info+5);

    if(new_UID_len > 0){
        new_card->uidlen = new_UID_len;
        memcpy(new_card->uid, new_UID, new_UID_len);
        Log(TRACE, "%p  %p   %p  %p", &new_card_info, new_card_info+5, (*new_card).uid, new_card->uid);
        Log(TRACE, "mole sends UID:%s", hex_text(mole_card->uid, mole_card->uidlen));
        Log(TRACE, "New MITM   UID:%s", hex_text(new_card->uid, new_card->uidlen));
    }
    if(new_ATQA_set){
        memcpy(new_card->atqa, new_ATQA, 2);
        Log(TRACE, "mole sends ATQA:%s", hex_text(mole_card->atqa, 2));
        Log(TRACE, "New MITM   ATQA:%s", hex_text(new_card->atqa, 2));
    }
    if(new_ATS_len > 0){
        new_card->ats_len = new_ATS_len;
        memcpy(new_card->ats, new_ATS, new_ATS_len);
        new_card_info[0] = ((new_card->ats_len+15+2) >> 0) & 0xFF;
        new_card_info[1] = ((new_card->ats_len+15+2) >> 8) & 0xFF;
    }
    if(new_TA1_set){
        if(new_card->ats_len >= 3 && new_card->ats[1] & 0x10){ 
            Log(TRACE, "mole sends TA1:%X", new_card->ats[2]);
            new_card->ats[2] = 0;
            Log(TRACE, "New MITM   TA1:%X", new_card->ats[2]);
        }else{
            Log(TRACE, "TA1 ... no ATS");
        }
    }
    if(new_SAK_set){
        new_card->sak = new_SAK;
        Log(TRACE, "mole sends SAK:%X", mole_card->sak);
        Log(TRACE, "New MITM   SAK:%X", new_card->sak);
    }
    if(new_FWI_set){
        new_card->ats[3] = new_card->ats[3] & 0x0F; // delete first 4 bits
        new_card->ats[3] = new_card->ats[3] | new_FWI;
    }
    if(new_SFGI_set){
        new_card->ats[3] = new_card->ats[3] & 0xF0; // delete last 4 bits
        new_card->ats[3] = new_card->ats[3] | new_SFGI;
    }
    if(new_ATS_len > 0 || new_FWI_set || new_SFGI_set){
        Log(TRACE, "mole sends ATS:%s", hex_text(mole_card->ats, mole_card->ats_len));
        Log(TRACE, "New MITM   ATS:%s", hex_text(new_card->ats, new_card->ats_len));
    }
    return new_card_info;
}

// Send settings to pm3 client(proxy)... this is called when pm client(proxy) sends RELAY_START cmd.
void sendArrayOfData(){ 
    for(int i = 0; i < arr_send_later_len; i++){
        if(arr_send_later[i]){
            uint16_t len = *arr_send_later[i] | ((*(arr_send_later[i]+1)) << 8);
            if(arr_send_later[i][3] == RELAY_SET_CHANGE){
                Log(TRACE, "Sending change settings   len:%u", len);
                SendToPipe(arr_send_later[i], len+3);
            }else if(arr_send_later[i][3] == RELAY_SET_QUICK_REPLY){
                Log(TRACE, "Sending quick reply settings   len:%u", len);
                SendToPipe(arr_send_later[i], len+3);
            }else if(arr_send_later[i][3] == RELAY_SET_INSERT){
                Log(TRACE, "Sending insert settings   len:%u", len);
                Send_WS(arr_send_later[i], len+3);
            }else if(arr_send_later[i][3] == RELAY_SET_TEST_TIME){
                Log(TRACE, "Sending time settings   len:%u", len);
                SendToPipe(arr_send_later[i], len+3);
            }
        }
    }
}   
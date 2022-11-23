//
#include <string.h>
#include "cmdhf14a.h"
#include "comms.h"
#include "util_posix.h"
#include "pipe.h"
#include "ui.h"
#include "cliparser.h"
#include "relay_proxy.h"
#include "cmdtrace.h"
#include "crc16.h"
#include "uart/uart.h"

// #include <string.h>

# define AddCrc14A(data, len) compute_crc(CRC_14443_A, (data), (len), (data)+(len), (data)+(len)+1)

static uint8_t *prevPacketFromReader = NULL;
static uint8_t *prevPacketFromTag = NULL;
static uint8_t prevPacketFromReader_len = 0;
static uint8_t prevPacketFromTag_len = 0;

static bool debug_msg = false;

static char* hex_text(uint8_t* buf, int len){
    static char hex[2048] = {0};
    memset(hex, 0, 2048);
    char *tmp = (char *)hex;

    for (int i = 0; i*3 < 2048 && i < len; ++i, tmp += 3) {
        sprintf(tmp, "%02X ", (unsigned int) buf[i]);
    }

    *tmp = '\0';
    return hex;
}

static void mitmChange(struct relay_change_data* rcd_s[], int rcd_s_len, uint8_t** in_out, bool fromTag){
    PacketToPipe* ptp = (PacketToPipe*)(*in_out);
    static uint8_t *new_raw = NULL;
    for(int i = 0; i < rcd_s_len; i++){
        bool tag = (rcd_s[i]->tag_prev & 2) == 2;
        if(ptp->length-2 != rcd_s[i]->what_len) continue; // if length not eq.
        if(tag == false && fromTag) continue;
        if(tag && fromTag == false) continue; 
        bool prev_1st = (rcd_s[i]->tag_prev & 1) == 1;
        if(prev_1st && tag && prevPacketFromReader_len < rcd_s[i]->prev_len) continue; // like startsWith() !!! 
        if(prev_1st == false && tag && prevPacketFromTag_len < rcd_s[i]->prev_len) continue; 
        if(prev_1st && tag == false && prevPacketFromTag_len < rcd_s[i]->prev_len) continue; 
        if(prev_1st == false && tag == false && prevPacketFromReader_len < rcd_s[i]->prev_len) continue; 

        uint8_t* what = rcd_s[i]->what;
        bool eq = false;
        if(debug_msg){
            PrintAndLogExPipe(INFO, "++++++++ len == ... %u", rcd_s_len);
        }
        for(int j = 0; j < rcd_s[i]->what_len; j++){ // match change bytes.
            bool x_bit_L = (rcd_s[i]->x_what[(j*2)/8] & (1 << ((j*2)%8) )); // X0
            bool x_bit_R = (rcd_s[i]->x_what[((j*2)+1)/8] & (1 << (((j*2)+1)%8) )); // 0X 
            uint8_t in = ptp->data[2+j];
            if(x_bit_L){
                in &= 0x0F;
            }
            if(x_bit_R){
                in &= 0xF0;
            }
            // PrintAndLogExPipe(INFO, "in: %02X  %02X    x_L:%u  x_R:%u", ptp->data[2+j], what[j], x_bit_L, x_bit_R);
            if(in == what[j]){ // EQ
                eq = true;
            }else{
                if(debug_msg){
                    PrintAndLogExPipe(INFO, "in B:%d   %02X != %02X    x_L:%u  x_R:%u", j, ptp->data[2+j], what[j], x_bit_L, x_bit_R);
                }
                eq = false;
                break;
            }
        }

        uint8_t *prevP = NULL;
        if((prev_1st && tag) || (prev_1st == false && tag == false)){
            prevP = prevPacketFromReader;
        }else if((prev_1st && tag == false) || (prev_1st == false && tag)){
            prevP = prevPacketFromTag;
        }
        
        for(int j = 0; j < rcd_s[i]->prev_len; j++){ // match prev bytes.
            bool x_bit_L = (rcd_s[i]->x_prev[(j*2)/8] & (1 << ((j*2)%8) )); // X0
            bool x_bit_R = (rcd_s[i]->x_prev[((j*2)+1)/8] & (1 << (((j*2)+1)%8) )); // 0X 
            uint8_t in = prevP[j];
            if(x_bit_L){
                in &= 0x0F;
            }
            if(x_bit_R){
                in &= 0xF0;
            }
            // PrintAndLogExPipe(INFO, "prev: %02X  %02X    x_L:%u  x_R:%u", prevP[j], rcd_s[i]->prev[j], x_bit_L, x_bit_R);
            if(in == rcd_s[i]->prev[j]){ // EQ
                eq = true;
            }else{
                if(debug_msg){
                    PrintAndLogExPipe(INFO, "prev B:%d   %02X != %02X    x_L:%u  x_R:%u", j, prevP[j], rcd_s[i]->prev[j], x_bit_L, x_bit_R);
                }
                eq = false;
                break;
            }
        }
        if(eq){
            PrintAndLogExPipe(INFO, "Found     %s", hex_text(rcd_s[i]->what, rcd_s[i]->what_len));
            PrintAndLogExPipe(INFO, "EQ        %s", hex_text(ptp->data+2, ptp->length-2));
            uint8_t crc = 0;
            if((rcd_s[i]->addCRC_setBN & 2) == 2) crc = 2;

            uint8_t* new_raw_tmp = realloc(new_raw, 5+rcd_s[i]->replace_with_len+crc);
            if (new_raw_tmp) {
                new_raw = new_raw_tmp;
            }
            memcpy(new_raw, (uint8_t*)ptp, 5);
            memcpy(new_raw+5, rcd_s[i]->replace_with, rcd_s[i]->replace_with_len);
            for(int k = 0; k < rcd_s[i]->replace_with_len; k++){
                bool x_bit_L = (rcd_s[i]->x_replace_with[(k*2)/8] & (1 << ((k*2)%8) )); // X0
                bool x_bit_R = (rcd_s[i]->x_replace_with[((k*2)+1)/8] & (1 << (((k*2)+1)%8) )); // 0X 
                if(x_bit_L || x_bit_R){
                    uint8_t out = 0;
                    if(x_bit_L){
                        out |= (0xF0 & ptp->data[2+k]);
                    }
                    if(x_bit_R){
                        out |= (0x0F & ptp->data[2+k]);
                    }
                    new_raw[5+k] = out;
                    if(debug_msg){
                        PrintAndLogExPipe(INFO, "pos:%d   i: %02X  o: %02X    x_L:%u  x_R:%u", k, ptp->data[2+k], out, x_bit_L, x_bit_R);
                    }
                }
            }
            if(crc != 0){
                AddCrc14A((new_raw+5), rcd_s[i]->replace_with_len);
            }
            new_raw[0] = ((5-3+rcd_s[i]->replace_with_len+crc) >> 0) & 0xFF; // update length
            new_raw[1] = ((5-3+rcd_s[i]->replace_with_len+crc) >> 8) & 0xFF;
            *in_out = new_raw;
            PrintAndLogExPipe(INFO, "CHANGE TO:%s", hex_text(new_raw+5, rcd_s[i]->replace_with_len+crc));
            break;
        }
        
    }
    ptp = (PacketToPipe*)(*in_out);
    if(fromTag){ // update previous packet.
        memcpy(prevPacketFromTag, ptp->data+2, ptp->length-2);
        prevPacketFromTag_len = ptp->length-2;
    }else{
        memcpy(prevPacketFromReader, ptp->data+2, ptp->length-2);
        prevPacketFromReader_len = ptp->length-2;
    }
}

static void ignorePMMsgs(size_t ms){
    PacketResponseNG resp;
    while (1) // ignore 
    {
        bool res = WaitForResponseTimeoutW(CMD_HF_ISO14443A_RELAY, &resp, ms, false);
        if(res == false){
            break;
        }else{
            PrintAndLogExPipe(WARNING, "Unexpected msg from proxmark (proxy) status:%d   cmd:%u", resp.status, resp.cmd);
        }
    }
}

int Proxy(const uint8_t dd){
    bool kill_called = false;

    PrintAndLogExPipe(INFO, "INIT PIPE");
    if(!OpenPipe()){
        PrintAndLogExPipe(FAILED, "Pipe is not working !");
        return PM3_EFILE;
    }

    uart_reconfigure_timeouts(1);
    struct relay_change_data *rcd_s[100];
    //struct relay_change_data **rcd_s = calloc(100, sizeof(struct relay_change_data *));
    int rcd_s_len = 0;

    uint8_t *pipe_data = NULL;
    ignorePMMsgs(100);
    int r = WaitPipeDate(10, &pipe_data);

    SendTxtToPipe("--START RELAY--");
    // Wait 
    r = WaitPipeDate(500, &pipe_data);
    if(r <= 3){
        PrintAndLogExPipe(FAILED, "Pipe test failed !");
        return PM3_EFILE;
    }else {
        char * strptr = strstr((char *)pipe_data+3, "--START RELAY--");
        if(strptr){
            PrintAndLogExPipe(INFO, "PIPE OK");
        }else{
            PrintAndLogExPipe(WARNING, "Pipe test failed... str not found. cmd:%u len:%u",pipe_data[2], pipe_data[0]);
            PrintAndLogExPipe(WARNING, "in: %s", hex_text(pipe_data, r));
            goto jump_end;
        }
    }

    clearCommandBuffer();
    SendCommandNG(CMD_HF_ISO14443A_RELAY, (uint8_t *)&dd, sizeof(uint8_t));

    PacketResponseNG resp;
    PacketToPipe ptp; 

    prevPacketFromReader = calloc(512, sizeof(uint8_t));
    prevPacketFromTag = calloc(512, sizeof(uint8_t));
//  Ping
    uint8_t data[2048] = {0};
    uint64_t t = msclock();
    uint64_t t1 = t;
    uint32_t num_bytes = 0;
    bool ping;
    bool ping_back;
    int r_len = 0;
    unsigned int ping_lim = 500;
    // bool skip_next_ping_send = false;
    unsigned int len = 1;
    // unsigned int prev_len = 0;
//
    bool time_for_relay = false;

    int not_ok_ping = 0;
    unsigned int num_ping =0;
    int array_ping_time[100] = {0};
    while(len < ping_lim){ // PING
        num_ping++;
        num_bytes += len;
        for (uint16_t i = 0; i < len; i++)
            data[i] = i & 0xFF & len;

        t1 = msclock();
        clearCommandBuffer();
        SendCommandNG(CMD_PING, data, len);
        len+=8;
        ping = false;
        ping_back = false;

        for(int i = 0; i<30; i++){
            bool res = WaitForResponseTimeoutW(CMD_HF_ISO14443A_RELAY, &resp, 200, false);
            if(res){
                if(resp.status == PM3_EFATAL){ // Button press.
                    PrintAndLogExPipe(INFO ,"PM3_EFATAL button pressed");
                    goto jump_end;
                }else if(resp.status == PM3_SNONCES){
                    bool error = (memcmp(data, resp.data.asBytes, resp.length) != 0);
                    // PrintAndLogExPipe((error) ? ERR : SUCCESS, " " _GREEN_("received") " %s  len: %u   time: %lu ms", error ? _RED_("NOT ok") : _GREEN_("OK"), resp.length, msclock() - t1);
                    PrintAndLogExPipe((error) ? ERR : SUCCESS, "received from proxy: %s  len: %u   time: %lu ms", error?"NOT ok":"OK", resp.length, msclock() - t1);
                    if(error){
                        goto jump_end;
                    }
                    ping = true;
                    SendDataToPipe(P_PING, resp.length, resp.data.asBytes);
                    break;
                }
            }else{
                PrintAndLogExPipe(WARNING, "(PING state) Unexpected msg or no msg from proxmark (proxy) status:%d   cmd:%u", resp.status, resp.cmd);
            }
        }
        if( ! ping){
            PrintAndLogExPipe(FAILED, "Ping proxmark response " _RED_("timeout"));
            goto jump_end;
        }

        r_len = WaitPipeDate(500, &pipe_data);
        if(r_len > 0){
            getPackets(pipe_data, r_len);
            while(1){
                PacketToPipe *p = NextPacket();
                if(p == NULL){
                    break;
                }
                if(p->cmd == P_PING){
                    bool error = (memcmp(data, p->data, (size_t)p->length) != 0);
                    PrintAndLogExPipe((error) ? ERR : SUCCESS, "Received back from Mole %s  len: %d   time: %d ms", error ? "NOT ok":"OK", resp.length, msclock() - t1);
                    array_ping_time[num_ping-1] = msclock() - t1;
                    ping_back = true;
                    if(error){not_ok_ping++;}
                }else if(p->cmd == P_KILL){
                    kill_called = true;
                    PrintAndLogExPipe(INFO, "killing PM3");
                    goto jump_end;
                }else{
                    PrintAndLogExPipe(WARNING ,"(PING state) Unexpected msg from Mole-Proxy. cmd:%u  len:%u", p->cmd, p->length);
                }
            }
        }
        if( ! ping_back){
            PrintAndLogExPipe(FAILED, "Ping response " _RED_("timeout"));
            goto jump_end;
        }
        if(len >= ping_lim){ // last ping msg.
            PrintAndLogExPipe(NORMAL, "PING total %u bytes in %lu ms  Avg. packet time: %u ms", num_bytes, msclock() - t, (msclock() - t)/num_ping);
            //
            for(int i=0; i < num_ping; i++){
                for(int j=0; j < num_ping-1; j++){
                    if(array_ping_time[j] > array_ping_time[j+1]){
                        int temp = array_ping_time[j];
                        array_ping_time[j] = array_ping_time[j+1];
                        array_ping_time[j+1] = temp;
                    }
                }
            }
            float median=0;
            if(num_ping%2 == 0){
                median = (array_ping_time[(num_ping-1)/2] + array_ping_time[num_ping/2])/2.0;
            }else{
                median = array_ping_time[num_ping/2];
            }
            PrintAndLogExPipe(NORMAL, "PING median %f ms", median);
            //
            time_for_relay = true;
            break;
        }
        if(not_ok_ping > 6){
            PrintAndLogExPipe(FAILED, _RED_("Ping responses NOT OK."));
            goto jump_end;
        }
    }
    bool intro = false;
    if(time_for_relay){
        ptp.length = 2;
        ptp.cmd = P_RELAY;
        ptp.data[0] = RELAY_START;
        ptp.data[1] = 0;
        SendPacketToPipe(&ptp);
        while (1)
        {   
            bool deviceReply = isReplyReady(&resp); // GET DATA FORM PM3 over USB.
            if(deviceReply){
                if(resp.status == PM3_EFATAL){ // Button press.  ??? a se nuca ?
                    PrintAndLogExPipe(INFO, "PM3_EFATAL button pressed");
                    goto jump_end;
                }else if(resp.status == PM3_EFAILED){
                    PrintAndLogExPipe(INFO, "PM3_EFAILED unknown cmd on pm3 side.");
                    continue;
                }
                if(resp.cmd != CMD_HF_ISO14443A_RELAY){
                    PrintAndLogExPipe(WARNING, "cmd is not CMD_HF_ISO14443A_RELAY ... %u", resp.cmd);
                    print_hex(resp.data.asBytes, resp.length);
                    continue;
                }
                if(resp.oldarg[0] == RELAY_RAW){ // Send RAW cmd to MOLE.
                    memcpy(ptp.data+2, resp.data.asBytes, resp.length);
                    ptp.length = resp.length+2;
                    ptp.cmd = P_RELAY;
                    ptp.data[0] = RELAY_RAW;
                    ptp.data[1] = 0;
                    PacketToPipe* tmp_ptp = &ptp;
                    mitmChange(rcd_s, rcd_s_len, (uint8_t**)&tmp_ptp, false);
                    // PrintAndLogExPipe(INFO, "CHANGE   :%s", hex_text((uint8_t*)tmp_ptp+5, tmp_ptp->length-2));
                    SendPacketToPipe(tmp_ptp);
                }else if(resp.oldarg[0] == RELAY_PROXY_END){ // proxy end
                    PrintAndLogExPipe(INFO, "RELAY_PROXY_END");
                    goto jump_end;
                }else if(resp.oldarg[0] == RELAY_PROXY_TRACE){ // trace
                    // set crc.
                    if(resp.data.asBytes[11] != 0){
                        PrintAndLogExPipe(INFO, "NOT 0 !!!!");
                    }
                    resp.data.asBytes[11] = check_crc(CRC_14443_A, resp.data.asBytes+13, ((PacketToPipe*)resp.data.asBytes)->length);
                    SendPacketToPipe((PacketToPipe*)resp.data.asBytes);
                }else{
                    PrintAndLogExPipe(INFO, "Unknown cmd from PM3 proxy c:%lu", resp.oldarg[0]);
                }
            }
            r_len = GetData(&pipe_data); // GET DATA FORM SERVER (PROXY)
            if(r_len > 0){
                getPackets(pipe_data, r_len);
                while(1){
                    PacketToPipe *p = NextPacket();
                    if(p == NULL){
                        break;
                    }else if(p->cmd == P_KILL){
                        //PrintAndLogExPipe(INFO, "killing PM3");
                        kill_called = true;
                        goto jump_end;
                    }else if(p->cmd == P_SEND_BACK){
                        p->cmd = P_SEND_BACK_2;
                        SendPacketToPipe(p);
                    }else if(p->cmd == P_SEND_BACK_2){
                        PrintAndLogExPipe(INFO, "%s", p->data);
                    }else if(p->cmd == P_RELAY){
                        uint8_t relay_cmd = p->data[0];
                        // uint8_t status = p->data[1]; // !!!!????
                        if(relay_cmd == RELAY_TAG_INFO){
                            PrintAndLogExPipe(INFO, "got card data... begin card emulation.");
                            clearCommandBuffer();
                            SendCommandMIX(CMD_HF_ISO14443A_RELAY, RELAY_TAG_INFO,0,0, p->data+2, p->length-2);
                            intro = true;
                        }else if(relay_cmd == RELAY_REPLY_RAW){
                            PrintAndLogExPipe(INFO, "Got response from MOLE... sending it to real Reader.");
                            if (intro){
                                PacketToPipe* tmp_p = p;
                                mitmChange(rcd_s, rcd_s_len, (uint8_t**)&tmp_p, true);
                                // PrintAndLogExPipe(INFO, "CHANGE   :%s", hex_text((uint8_t*)tmp_p+5, tmp_p->length-2));
                                clearCommandBuffer();
                                SendCommandMIX(CMD_HF_ISO14443A_RELAY, RELAY_REPLY_RAW, 0, 0, tmp_p->data+2, tmp_p->length-2);
                            }else{
                                PrintAndLogExPipe(FAILED, "Raw cmd before Intro(card info))");
                            }
                        }else if(relay_cmd == RELAY_SET_CHANGE){
                            PrintAndLogExPipe(INFO, "RELAY_SET_CHANGE %u", sizeof(relay_change_data));
                            uint8_t* rcd_data = calloc(p->length-2, sizeof(uint8_t));
                            memcpy(rcd_data, p->data+2, p->length-2);
                            rcd_s[rcd_s_len] = (relay_change_data*)rcd_data;
                            int offset = sizeof(relay_change_data);
                            rcd_s[rcd_s_len]->what = rcd_data + offset;
                            offset += rcd_s[rcd_s_len]->what_len;
                            rcd_s[rcd_s_len]->replace_with = rcd_data + offset;
                            offset += rcd_s[rcd_s_len]->replace_with_len;
                            rcd_s[rcd_s_len]->prev = rcd_data + offset;
                            offset += rcd_s[rcd_s_len]->prev_len;

                            int x_what_len = (rcd_s[rcd_s_len]->what_len*2/8);
                            if(((rcd_s[rcd_s_len]->what_len*2)%8) != 0) x_what_len++;
                            int x_replace_with_len = (rcd_s[rcd_s_len]->replace_with_len*2/8);
                            if(((rcd_s[rcd_s_len]->replace_with_len*2)%8) != 0) x_replace_with_len++;

                            rcd_s[rcd_s_len]->x_what = rcd_data + offset;
                            offset += x_what_len;
                            rcd_s[rcd_s_len]->x_replace_with = rcd_data + offset;
                            offset += x_replace_with_len;
                            rcd_s[rcd_s_len]->x_prev = rcd_data + offset;

                            rcd_s_len++;

                        }else if(relay_cmd == RELAY_SET_QUICK_REPLY){
                            PrintAndLogExPipe(INFO, "RELAY_SET_QUICK_REPLY %u", p->length);
                            SendCommandMIX(CMD_HF_ISO14443A_RELAY, RELAY_SET_QUICK_REPLY, 0, 0, p->data+2, p->length-2);
                        }else if(relay_cmd == RELAY_SET_TEST_TIME){
                            SendCommandMIX(CMD_HF_ISO14443A_RELAY, RELAY_SET_TEST_TIME, 0, 0, p->data+2, p->length-2);
                        }else if(relay_cmd == RELAY_SET_TEST_TIME4){
                            SendCommandMIX(CMD_HF_ISO14443A_RELAY, RELAY_SET_TEST_TIME4, 0, 0, p->data+2, p->length-2);
                        }else if(relay_cmd == RELAY_PROXY_END){
                            goto jump_end;
                        }else{
                            PrintAndLogExPipe(WARNING, "Unknown P_RELAY. cmd:%u  len:%u", relay_cmd, p->length);
                        }
                    }else{
                        PrintAndLogExPipe(WARNING, "(RELAY state) Unexpected msg from Mole-Proxy. cmd:%u  len:%u", p->cmd, p->length);
                    }
                }
            }
        }
    }
    
jump_end:
    PrintAndLogExPipe(INFO, "--END RELAY--");
    if( ! kill_called){
        SendDataToPipe(P_KILL, 0, NULL);
        msleep(300);
    }
    SendCommandNG(CMD_BREAK_LOOP, NULL, 0);
    SendTxtToPipe("--END RELAY--");
    ClosePipe();
    
    for(int i = 0; i < rcd_s_len; i++){ // free
        if(rcd_s[rcd_s_len]){
           free(rcd_s[rcd_s_len]);
        }
    }
    return PM3_SUCCESS;
}

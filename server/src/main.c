// Parts of the code are from:
// Embedded Web Server https://mongoose.ws/tutorials/websocket-server/
// book "Advanced Programming in the Unix Environment"  TTY http://www.apuebook.com/code3e.html

#include <errno.h>
#include "mongoose.h"
#include "pipe.h"
#include "files.h"
#include "pipe_data.h"
#include "tty.h"
#include "ws.h"
#include <signal.h>
#include "log.h"
#include "relay.h"
#include "mole.h"
// #include "stdio.h"
// #include <strlib.h>

#if defined(__APPLE__)
#define PATH_RELAY "$HOME/Documents/proxmark3-relay/"
#else
#define PATH_RELAY "$HOME/proxmark3-relay/"
#endif

#define BUFFSIZE 1024 * 100
void exit_all();


static bool mole = false;
static uint32_t ip_relay_link;
static uint16_t port_relay_link;

static uint32_t ip_monitor;
static uint16_t port_monitor;

static struct mg_connection *con = NULL;
static struct mg_connection *con_relay_link;
static char ip_buf[50];
static const char *s_listen_on = "ws://0.0.0.0:8000";
static const char *s_web_root = ".";
static int fdm = -1;
struct mg_mgr mgr;

struct timeval startTime, endTime, startLoop, endLoop;
static long loop_u = 0;
static uint8_t *pipe_data;

static const char new_data_msg[] = "file updated.";


static int nread;
static char buf[BUFFSIZE];

// Kill Proxmark3 client
static void killPM3(void){
  // Kill script "pm3" and kill program "proxmark3".... 
  // Works on Linux and Mac.
  Log(WARNING, "KILLING PM3 client if exist.");
  system("pgrep -x proxmark3 >/dev/null && killall -9 'proxmark3' || echo 'Proxmark process not found'");
  system("ps -ef | grep 'pm3 -c' | grep -v grep | awk '{print $2}' | xargs kill");
}

static void poweroff(){ // Works on Linux
  killPM3();
  usleep(500 * 1000);
  Log(INFO, "POWEROFF");
  mg_mgr_poll(&mgr, 0);
  usleep(1000*10);
  mg_mgr_free(&mgr);
  UnlinkPipe();
  CloseLogFile();
  system("sudo poweroff");
  exit(0);
}

static void fn_mole(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if(fn_data == NULL){} 
  if (ev == MG_EV_ERROR) {
    LOG(LL_ERROR, ("%p %s", c->fd, (char *) ev_data));
  } else if (ev == MG_EV_WS_OPEN) {
    SetC(c);
    SetM(&mgr);
    Log(TRACE, "WS OPEN");
    SendText_WS("MOLE ONLINE", 5);
    // mg_ws_send(c, test_con_msg, strlen(test_con_msg), WEBSOCKET_OP_TEXT);
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    uint8_t *data = (uint8_t *)wm->data.ptr;
    PacketToPipe *ptp = (PacketToPipe*)data;

    if(ptp->cmd == P_PING){ // deep ping
      Log(TRACE, "ping");
      SendToPipe((uint8_t *)wm->data.ptr, wm->data.len);
      //mg_ws_send(c, (char *)data, wm->data.len, WEBSOCKET_OP_BINARY);
      // !
    }else if(ptp->cmd == P_SEND_BACK_2){ // print
      Log(INFO, "%s", ptp->data);
    }else if(ptp->cmd == P_SEND_BACK){
      ptp->cmd = P_SEND_BACK_2;
      mg_ws_send(c, (char *)data, wm->data.len, WEBSOCKET_OP_BINARY);    
    }else if(ptp->cmd == P_KILL){
      Log(INFO, "kill pm3");
      SendToPipe((uint8_t *)wm->data.ptr, wm->data.len);
      usleep(700 * 1000);
      killPM3();
    }else if(ptp->cmd == P_POWEROFF){
      poweroff();
    }else if(ptp->cmd == P_LOG){ // not needed
      // Save to file.
    }else if(ptp->cmd == P_RELAY && ptp->data[0] == 0){
      // RUN pm3 but first kill pm3 if is running 
      killPM3();
      char * cmd = calloc(512, sizeof(char));
      strcat(cmd, PATH_RELAY);
      strcat(cmd, "pm3 -c 'hf 14a relay_mole");
      strcat(cmd, "' >> "); // ' -i
      strcat(cmd, getPm3LogPath());
      strcat(cmd, " 2>&1\r");

      ExeCtrlC();
      ExeCmd(cmd, true);

      free(cmd);
    }else if(ptp->cmd == P_RELAY){ // other to pm3
      SendToPipe((uint8_t *)wm->data.ptr, wm->data.len);
      //Log(TRACE, "P_RELAY");
    }else{
      Log(INFO, "unexpacted cmd:%u send to mole... len:%u", ptp->cmd, ptp->length);
    }
  }else if(ev == MG_EV_CLOSE){
    Log(TRACE, "ev close");
    con = NULL;
    SetC(NULL);
  }else{
    // if(ev == MG_EV_POLL)
  }
}

static void fn_proxy(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if(fn_data == NULL){} 
  if (ev == MG_EV_OPEN) {
  } else if (ev == MG_EV_CLOSE){
    if(ip_relay_link == c->peer.ip && port_relay_link == c->peer.port){
      SetC(NULL);
      Log(INFO, "Mole DISCONNECTED.");
      if(con != NULL){
        mg_ws_send(con, "mole offline.", 13, WEBSOCKET_OP_TEXT);
      }
    }
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    if (mg_http_match_uri(hm, "/websocket")) {
      // Upgrade to websocket. From now on, a connection is a full-duplex
      // Websocket connection, which will receive MG_EV_WS_MSG events.
      ip_monitor = c->peer.ip;
      port_monitor = c->peer.port;
      mg_straddr(&c->peer, ip_buf, sizeof(ip_buf));
      Log(INFO, "OPEN /websocket  %s", ip_buf);
      mg_ws_upgrade(c, hm, NULL);
    } else if (mg_http_match_uri(hm, "/ws_relay_link")) {
      ip_relay_link = c->peer.ip;
      port_relay_link = c->peer.port;
      SetC(c);
      SetM(&mgr);
      if(con != NULL){
        mg_ws_send(con, "mole online.", 12, WEBSOCKET_OP_TEXT);
      }
      mg_straddr(&c->peer, ip_buf, sizeof(ip_buf));
      Log(INFO ,"MOLE ONLINE ? OPEN /ws_relay_link  %s", ip_buf);
      mg_ws_upgrade(c, hm, NULL);
    } else if (mg_http_match_uri(hm, "/api/poweroff") && strncmp((char *)hm->method.ptr, "POST", 4) == 0) {
      mg_http_reply(c, 200, "", "OK");
      if (strstr((char *)hm->body.ptr, "mole=on")){
        Log(INFO, "MOLE POWEROFF");
        if(Is_WS_Null()){
          Log(FATAL, "mole NOT connected !");
          mg_http_reply(c, 200, "", "mole NOT connected !");
        }else {
          PacketToPipe ptp;
          ptp.cmd = P_POWEROFF;
          ptp.length = 0;
          Send_WS((uint8_t*)&ptp, 3);
          mg_mgr_poll(&mgr, 0);
        }
      }
      if (strstr((char *)hm->body.ptr, "proxy=on")){
        Log(INFO, "PROXY POWEROFF");
        poweroff();
      }
      

    } else if (mg_http_match_uri(hm, "/api/start_sniff") && strncmp((char *)hm->method.ptr, "POST", 4) == 0) {
      Log(INFO, "<%.*s>", (int)hm->body.len, (char *)hm->body.ptr);
      killPM3();
      char * cmd = calloc(512, sizeof(char));
      strcat(cmd, PATH_RELAY);
      strcat(cmd, "pm3 -c 'hf 14a sniffo");
      if (strstr((char *)hm->body.ptr, "checkboxSniffPipe=on")){
        Log(TRACE ,"pipe on");
        strcat(cmd, " -p");
      }
      if (strstr((char *)hm->body.ptr, "checkboxSniffCard=on")){
        Log(TRACE, "card on");
        strcat(cmd, " -c");
      }
      if (strstr((char *)hm->body.ptr, "checkboxSniffReader=on")){
        Log(TRACE, "device on");
        strcat(cmd, " -r");
      }
      strcat(cmd, "'\r"); // ' -i

      ExeCtrlC();
      ExeCmd(cmd, true);

      mg_http_reply(c, 200, "", "OK. Executing: %s", cmd);
      free(cmd);
    } else if (mg_http_match_uri(hm, "/api/lastSniffFile")) {

      char * lastFile = getLastFileName();
      if(lastFile == NULL){
        mg_http_reply(c, 200, "", "No file found...\n", lastFile);
      }
      mg_http_reply(c, 200, "", "File: %s", lastFile);
    } else if(mg_http_match_uri(hm, "/api/start_relay") && strncmp((char *)hm->method.ptr, "POST", 4) == 0){
      Log(INFO, "/api/start_relay -> '%.*s'", (int)hm->body.len, (char *)hm->body.ptr);
      parseStartRelay((int)hm->body.len, (char *)hm->body.ptr); // get settings

      PacketToPipe ptp;
      ptp.cmd = P_RELAY;
      ptp.length = 1; // 3
      ptp.data[0] = 0;
      if(Is_WS_Null()){
        Log(FATAL, "mole NOT connected !");
        mg_http_reply(c, 200, "", "mole NOT connected !");
      }else {
        Send_WS((uint8_t*)&ptp, 4); // ??? 3
        mg_http_reply(c, 200, "", "OK");
      }
    } else if(mg_http_match_uri(hm, "/api/stop_relay") && strncmp((char *)hm->method.ptr, "POST", 4) == 0){
      if(! Is_WS_Null()){
        SendText_WS("", P_KILL);
      }
      PacketToPipe ptp;
      ptp.cmd = P_KILL;
      ptp.length = 0;
      SendToPipe((uint8_t*)&ptp, 3);
      usleep(500 * 1000);
      killPM3();
      mg_http_reply(c, 200, "", "OK ... PRESS BUTTON !");
    }else {
      // Server static files
      //resolved = realpath("/proc/self/exe", buffer);
      struct mg_http_serve_opts opts = {.root_dir = s_web_root};
      mg_http_serve_dir(c, ev_data, &opts);
    }
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    
    if(c->peer.ip == ip_relay_link && c->peer.port == port_relay_link){
      //uint8_t *data = (uint8_t *)wm->data.ptr;
      uint8_t *data = calloc(wm->data.len+1, sizeof(char));
      memcpy(data, (uint8_t *)wm->data.ptr, wm->data.len);
      data[wm->data.len] = '\0';

      PacketToPipe *ptp = (PacketToPipe*)data;
      mg_straddr(&c->peer, ip_buf, sizeof(ip_buf));
      // printf("%s  cmd:%u  len:%u  '%s'\n", ip_buf, ptp->cmd, ptp->length, ptp->data);

      // printf("PTP cmd:%u  len:%u\n", ptp->cmd, ptp->length);

      if(ptp->cmd == P_PING){
        printf("ping\n");
        SendToPipe((uint8_t *)wm->data.ptr, wm->data.len);
      }else if(ptp->cmd == P_SEND_BACK){ // reply back.
        printf("heartbeat OK %u\n", wm->flags);
        ptp->cmd = P_SEND_BACK_2;
        mg_ws_send(c, (char *)data, wm->data.len, WEBSOCKET_OP_BINARY);  
      }else if(ptp->cmd == P_SEND_BACK_2){
        printf("%s\n", ptp->data);
      }else if(ptp->cmd == P_KILL){
        printf("killing PM3\n");
        SendToPipe((uint8_t *)wm->data.ptr, wm->data.len);
        usleep(700 * 1000);
        killPM3();
      }else if(ptp->cmd == P_LOG){
        //save to file...
        if(ptp->data[ptp->length-2] == 0){
          Log(TRACE, "->  last char not 0.");
        }
        AppendToLogFile((unsigned int)ptp->data[0], (unsigned int)ptp->data[1], (char *)ptp->data+2, ptp->length-2);
      }else if(ptp->cmd == P_RELAY_MOLE_TRACE){
        saveRelayTrace(true, data, true);
        // Log(TRACE, "P_RELAY_MOLE_TRACE %u", ptp->length-10);
      }else if(ptp->cmd == P_INFO && startsWith("--MOLE STARTED--", (char *)ptp->data)){
        char * cmd = calloc(512, sizeof(char));
        strcat(cmd, PATH_RELAY);
        strcat(cmd, "pm3 -c 'hf 14a relay");
        strcat(cmd, "'\r");
        killPM3();
        ExeCtrlC();
        ExeCmd(cmd, true);
        free(cmd);
      }else if(ptp->cmd == P_RELAY){
        if(ptp->data[0] == RELAY_TAG_INFO){ // Card info.
          uint8_t* newValues = setNewValues((uint8_t *)wm->data.ptr);
          uint16_t msgLen = newValues[0] | (newValues[1] << 8);

          Log(TRACE, "CARD INFO: %s", hex_text(newValues, msgLen+3));
          SendToPipe(newValues, msgLen+3);
        }else{
          SendToPipe((uint8_t *)wm->data.ptr, wm->data.len);
        }
        // Log(TRACE, "P_RELAY 0x%X", wm->data.ptr[5]);


      }else{
        Log(INFO, "unexpected cmd:%u send to proxy... len:%u", ptp->cmd, ptp->length);
      }
      free(data);
      // save to file
      // send to proxmark client if all ok.

      // printf("sending reply\n");      
      // mg_ws_send(c, wm->data.ptr, wm->data.len, WEBSOCKET_OP_TEXT);
    }
    else if(c->peer.ip == ip_monitor && c->peer.port == port_monitor){
      if (wm->data.len == 23 && prefix("size: c:", wm->data.ptr)) {  // setting window size
        changeWindowSize(wm->data.ptr);
      }else if(prefix("Where is log file?", wm->data.ptr)){
        char * text = (char *)calloc(400, sizeof(char));
        strcat(text, "log file is here:");
        strcat(text, GetLogLoc());
        mg_ws_send(con, text, 400, WEBSOCKET_OP_TEXT);
        free(text);
        resetLestLogText();
        // long len = readLogs();
        // if(len > 0){
        //   mg_ws_send(con, getLastLogText(), len, WEBSOCKET_OP_TEXT);
        // }
      }else if(prefix("Is Mole Online?", wm->data.ptr)){
        if(Is_WS_Null()){
          mg_ws_send(c, "mole offline.", 13, WEBSOCKET_OP_TEXT);
        }else {
          mg_ws_send(c, "mole online.", 12, WEBSOCKET_OP_TEXT);
        }
      }else { // terminal
        ExeCmdL(wm->data.ptr, wm->data.len, false);
        // errno = 0;
        // if (writen(fdm, wm->data.ptr, wm->data.len) != (ssize_t) wm->data.len){
        //   Log(ERROR, "writen error to stdout %s", strerror(errno));
        // }
      }
    }
    else{
      Log(ERROR, "msg from unknown IP!");
    }
  }
  //(void) fn_data;
}

// Ctrl + C handler
void sigintHandler(int sig_num)
{
    Log(TRACE, "%d Ctrl+C", sig_num);
    // if(mole){
      killPM3();
      // send kill
      if(con){
        SendText_WS("", P_KILL);
      }
    // }
    mg_mgr_poll(&mgr, 10);
    usleep(1000 * 1000);
    mg_mgr_free(&mgr);
    UnlinkPipe();
    CloseLogFile();
    exit(0);
}

int main( int argc, char *argv[] ) {
  unsigned int cc_t = 0;
  int length = 0;
  OpenLogFile(argc == 2);
  signal(SIGINT, sigintHandler);

  if( argc == 2 ) {
    Log(INFO, "THIS IS MOLE   arg: %s", argv[1]);
    mole = true;
  }
  else if( argc > 2 ) {
    Log(ERROR, "Too many arguments supplied.");
    exit(0);
  }
  else {
    Log(INFO, "THIS IS RELAY");
  }

  mg_log_set("3");
  Log(TRACE, "START PIPE INIT");
  CreatePipeIfNotExist();
  OpenPipe(true);
  Log(TRACE, "PipeInit DONE... Creating tty.");
  fdm = tty_new();
  Log(TRACE, "tty created. fdm:%d", fdm);
  mg_mgr_init(&mgr);

  gettimeofday(&startLoop, NULL);
  gettimeofday(&endLoop, NULL);
  //
  // MOLE
  //
  if(mole){
    char *relay_url = (char *)calloc(200, sizeof(char));
    strcat(relay_url, "ws://");
    strcat(relay_url, argv[1]); // ip:port
    strcat(relay_url, "/ws_relay_link");
    Log(INFO, "Relay url is: %s", relay_url);

    while (1){
      loop_u = endLoop.tv_usec - startLoop.tv_usec;
      gettimeofday(&startLoop, NULL);
      if(loop_u < 200){
        usleep(100);
      }
      if(con == NULL){
        con = mg_ws_connect(&mgr, relay_url, fn_mole, NULL, NULL);
      }
      mg_mgr_poll(&mgr, 0);
      if(con == NULL){ // if not connected to proxy ... try again
        usleep(2000 * 1000); // 1s
        continue;
      }

      // PIPE
      length = GetData(&pipe_data);

      if(length > 0){
        getPackets(pipe_data, length);
        while(1){
          // gettimeofday(&startTime, NULL);
          PacketToPipe *p = NextPacket();
          if(p == NULL){
            break;
          }
          if(p->cmd == P_SEND_BACK){
            p->cmd = P_SEND_BACK_2;
            SendToPipe((uint8_t*)p, p->length+3);
          }else if(p->cmd == P_SEND_BACK_2){
            Log(TRACE, "SEND BACK 2: %s", p->data);
          }else if(p->cmd == P_PING){
            Log(TRACE, "sending back ping data");
            Send_WS((uint8_t*)p, p->length+3);
          }else if(p->cmd == P_LOG){
            AppendToLogFile(((uint8_t *)p)[3], ((uint8_t *)p)[4], (char*)(p->data+2), p->length-2);
            Send_WS((uint8_t*)p, p->length+3);
          }else if(p->cmd == P_RELAY){
            // Log(INFO, "relay cmd %u from pm3...%X %X", p->data[0], p->data[2], p->data[3]);
            Send_WS((uint8_t*)p, p->length+3);
          }else if(p->cmd == P_KILL || p->cmd == P_RELAY_MOLE_TRACE){
            Send_WS((uint8_t*)p, p->length+3);
          }else if(p->cmd == P_INFO){
            if(startsWith("--START MOLE--", (char *)p->data)){ // from pm client
              Log(INFO, "mole started");
              SendToPipe((uint8_t *)p, p->length+3);
              SendText_WS("--MOLE STARTED--", P_INFO);
            }else if(startsWith("--END MOLE--", (char *)p->data)){ // from pm client
              Log(INFO, "mole stoped.");
            }else{
              Log(INFO, "info:%s", (char *)p->data);
            }
          }else{
            Log(ERROR, "NOT valid massage cmd: %u  len: %u  data: '%s'",p->cmd, p->length, (char *)p->data);
          }
          // gettimeofday(&endTime, NULL);
          // long micros = endTime.tv_usec - startTime.tv_usec;
          // Log(TRACE ,"t(%u)  %lu us", p->cmd, micros); // time ... avg. 150us
        }
      }
    
      // Terminal .... ignore
      if (fdm != -1 && cc_t%1000 == 0) {
        errno = 0;
        nread = read(fdm, buf, BUFFSIZE);

        if (nread == -1) {   // no data
          if (errno == EIO){  // exit linux
            goto jump_end;
          }
        } else if (nread <= 0) {  // exit mac os
          goto jump_end;
        } else {
          SaveTtyOutput(buf);
          memset(buf, 0, BUFFSIZE);
        }
      }
      gettimeofday(&endLoop, NULL);
      cc_t++;
    }
    mg_mgr_free(&mgr);
    return 0;
  }

  //
  // PROXY
  //

  unsigned int countLoop = 10000;
  Log(INFO, "Starting WS listener on %s/websocket", s_listen_on);
  mg_http_listen(&mgr, s_listen_on, fn_proxy, NULL);  // Create HTTP listener
  
  for (;;) {
    loop_u = endLoop.tv_usec - startLoop.tv_usec;
    gettimeofday(&startLoop, NULL);
    if(loop_u < 200){
      usleep(50);
    }
    mg_mgr_poll(&mgr, 0);  // Infinite event loop 0 ms
    con = NULL;
    con_relay_link = NULL;
    for( struct mg_connection* cc = mgr.conns; cc != NULL; cc = cc->next ){ // Get active connection.
      if( cc->is_accepted && cc->is_websocket && cc->peer.ip == ip_monitor && cc->peer.port == port_monitor){
        con = cc;
      }else if( cc->is_accepted && cc->is_websocket && cc->peer.ip == ip_relay_link && cc->peer.port == port_relay_link){
        con_relay_link = cc; // ?? do i need this ?!
      }
    }

    // PIPE
    length = GetData(&pipe_data);

    if(length > 0){
      getPackets(pipe_data, length);
      handleNewPackets();
      if(anyNewMsg()){
        countLoop = 0;
      }
    }
    if(countLoop++ == 1000 && con){
      mg_ws_send(con, new_data_msg, strlen(new_data_msg), WEBSOCKET_OP_TEXT);
    }

    // Send Logs
    if(cc_t % 1000 == 0 && con){
      long len = anyNewLog();
      if(len > 0){
        printf("%ld\n", len);
        fflush(stdout);
        char *buf_log = (char*)calloc((len+80), sizeof(char));
        memcpy(buf_log, "#$&()&$#", 8);
        memcpy(buf_log+8, getPtrToLastLogText(), len); // copy old logs
        mg_ws_send(con, buf_log, len+8, WEBSOCKET_OP_TEXT);
        free(buf_log);
      }
    }
    cc_t++;

    // TTY // Terminal in browser
    if (cc_t % 700 == 0 && fdm != -1) {
      errno = 0;
      nread = read(fdm, buf, BUFFSIZE);
      if (nread == -1) {   // no data
        if (errno == EIO){  // exit linux
          goto jump_end;
        }
      } else if (nread <= 0) {  // exit mac os
        goto jump_end;
      } else {
        if(con){
          mg_ws_send(con, buf, nread, WEBSOCKET_OP_TEXT);
        }
      }
    }
    gettimeofday(&endLoop, NULL);
  }

jump_end:
  Log(DEBUG, "nread:%d  errno:%s", nread, strerror(errno));
  if(con != NULL){
    mg_ws_send(con, buf, nread, WEBSOCKET_OP_CLOSE);
  }
  usleep(1000 * 1000);
  mg_mgr_free(&mgr);
  printf("exit 0\n");
  fflush(stdout);
  UnlinkPipe();
  return 0;
}

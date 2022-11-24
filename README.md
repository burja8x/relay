# Relay (ISO 14443-4, type A)

The tool is the product of a master's thesis (A tool for performing relay attacks on RFID/NFC systems) written in Slovenian by Andrej Burja.

Relay attack works best when using standard ISO/IEC 14443-4 type A.

The purpose of the tool is to perform a simple relay attack. We can also change and add mutual communication. The tool has a user interface that is used to set up the attack and display the execution of the attack.

How the tool works is shown in the image below.
![how_it_works](https://github.com/burja8x/relay/blob/main/img/relay_en.png?raw=true)

For the tool to work, we need two computers and two Proxmark3 devices.
Tested on macOS (silicon), Raspberry Pi OS.
Tested with Proxmark3 Easy and Proxmark RDV2.

The tool is using other tools/libraries:
- modified code [Iceman Fork Proxmark3](https://github.com/RfidResearchGroup/proxmark3 "Iceman Fork Proxmark3") (Release v4.14831 - Frostbit)
- Mongoose Web Server Library


###How it works
- Mole selects a tag (passes the obtained data (UID, ATQA, SAK, ATS) to the proxy device).
- The proxy reads the data and starts the emulation phase. The proxy thus fully imitates the tag.
- Then relay communication flows (RAW commands are transmitted).

The tool uses additional commands (WTX) to try to gain more time if necessary.

###Testing
- Tested on access control using MIFARE DESFire EV1, EV2 (without "Proximity check" functionality used)
- Tested between tag DESFire and TagInfo app (Android).
- It also works when we use a smartphone (Android) in emulation mode.
- etc.

Relay attack does not work on systems using MIFARE Classic (relay attack is too slow).

During testing, we found that the relay tool (in our case) delayed each communication (in both directions) by an average of 25 ms.

###User interface
You can easily change tag information (UID, ATQA, SAK, ATS) in the user interface. You can also change the rest of the communication, if you don't know the block numbers and CRC, you can use a latter "X" in these places. This allows any character to be in that position.

User interface has 5 tabs:
- **Terminal** Is a normal console. (don't forget that you expose the console to everyone on the network! If you want, you can comment in the code, the input data that comes from the network)
- **Sniff** This is basically the `hf 14a sniffo` command... real-time eavesdropping. To stop the command, press the key on proxmark3.
- **Relay** A normal relay attack needs no setup. To start, click the start button. If you want to change the communication, you can adjust the settings.
- **History** In this tab you can view the communication history (sniff, mole, proxy).
- **Logs** When you start a relay attack, this tab is the most useful, it shows communication and all events...


### Install the required dependencies

`sudo apt-get install libbz2-dev libreadline-dev clang binutils tmux gcc-arm-none-eabi`

If you are missing some default tools, you can find other dependencies on the "Iceman Fork - Proxmark3" Github page.

### Compile and upload

```bash
cd proxmark3-relay 
make clean && make
./pm3-flash-all
```

If you have problems compiling the code, take a look [Iceman Fork Proxmark3](https://github.com/RfidResearchGroup/proxmark3 "Iceman Fork Proxmark3") instructions.

```bash
cd ~/server/src && make clean && make
```

### Run

**Code is provided solely for educational purposes and/or testing on your own systems. We will not be responsible for any loss or damage whatsoever caused.**

Run on proxy side: 
`./relay`

Run on mole side: 
`./relay {proxy_ip}:8000`

When the two programs are running, the user interface is accessed at http://{proxy_ip}:8000/term.html

For easier loading and running programs, see the scripts: `auto_run_relay.sh, pi_sync_code.sh, pi_sync_code_proxy.sh`.
We recommend that you use a certificate and not a password for ssh login.

Changes in the Proxmark code:
added/changed in armsrc/ folder:
 - proxy.c
 - iso14443a.c
 - appmain.c

added/changed to the client/src/ folder:
 - cmdhf14a.c
 - pipe.c
 - relay_mole.c
 - relay_proxy.c


![user_interface_mitm](https://github.com/burja8x/relay/blob/main/img/mitm.png?raw=true)
![user_interface_logs](https://github.com/burja8x/relay/blob/main/img/Logs.png?raw=true)
![user_interface_change](https://github.com/burja8x/relay/blob/main/img/change.png?raw=true)
![user_interface_insert](https://github.com/burja8x/relay/blob/main/img/insert.png?raw=true)
![user_interface_quick](https://github.com/burja8x/relay/blob/main/img/quick.png?raw=true)
![user_interface_time](https://github.com/burja8x/relay/blob/main/img/test-time.png?raw=true)

![mole](https://github.com/burja8x/relay/blob/main/img/mole.jpg?raw=true)
![proxy](https://github.com/burja8x/relay/blob/main/img/proxy.jpg?raw=true)

![history1](https://github.com/burja8x/relay/blob/main/img/history1.png?raw=true)
![history2](https://github.com/burja8x/relay/blob/main/img/history2.png?raw=true)

![user_interface_terminal](https://github.com/burja8x/relay/blob/main/img/terminal.png?raw=true)
![user_interface_sniffo](https://github.com/burja8x/relay/blob/main/img/sniffo-tab.png?raw=true)
![user_interface_sniffo_t](https://github.com/burja8x/relay/blob/main/img/sniffo.png?raw=true)

![uid](https://github.com/burja8x/relay/blob/main/img/uid.png?raw=true)
![ats](https://github.com/burja8x/relay/blob/main/img/ats.png?raw=true)

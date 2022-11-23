#include <stdbool.h>


bool isValid(uint8_t *data, int length, int offset, int arrayOffset);
bool readSniff(uint8_t * data, int offset);
void save(uint8_t * pipe_data, bool print);
void saveRelayTrace(bool mole, uint8_t * pipe_data, bool print);
char* GenerateFileName(char * beginning, int num);
void createDataFile(char * dir, char * typeStr);
void clearLastFile(void);
char* getLastFileName(void);
bool anyNewMsg();
void getPackets(uint8_t *data, int length);
void handleNewPackets(void);
bool startsWith(const char *pre, const char *str);
PacketToPipe* NextPacket(void);
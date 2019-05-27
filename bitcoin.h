#ifndef _BITCOIN_H_
#define _BITCOIN_H_ 1

#include "protocol.h"

extern int nCurrentBlock;
extern unsigned char cfg_message_start[4];
bool TestNode(const CService &cip, int &ban, int &client, std::string &clientSV, int &blocks, bool &insync, std::vector<CAddress>* vAddr);

#endif

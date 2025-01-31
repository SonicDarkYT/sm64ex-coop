#include <stdio.h>
#include "../network.h"
#include "game/save_file.h"
#include "pc/debuglog.h"

void network_send_save_file(s32 fileIndex) {
    if (gNetworkPlayerServer == NULL) { return; }
    SOFT_ASSERT(gNetworkType == NT_CLIENT);
    struct Packet p = { 0 };
    packet_init(&p, PACKET_SAVE_FILE, true, PLMT_NONE);
    packet_write(&p, &fileIndex, sizeof(s32));
    network_send_to(gNetworkPlayerServer->localIndex, &p);
}

void network_receive_save_file(struct Packet* p) {
    if (gNetworkType != NT_SERVER) { return; }
    s32 fileIndex = 0;
    packet_read(p, &fileIndex, sizeof(s32));
    save_file_do_save(fileIndex, FALSE);
}

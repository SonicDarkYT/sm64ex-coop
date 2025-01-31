#include <stdio.h>
#include "../network.h"
#include "game/save_file.h"
#include "buffers/buffers.h"
#include "pc/debuglog.h"

void network_send_save_set_flag(s32 fileIndex, s32 courseIndex, u8 courseStars, u32 flags) {
    struct Packet p = { 0 };
    packet_init(&p, PACKET_SAVE_SET_FLAG, true, PLMT_NONE);
    packet_write(&p, &fileIndex,   sizeof(s32));
    packet_write(&p, &courseIndex, sizeof(s32));
    packet_write(&p, &courseStars, sizeof(u8));
    packet_write(&p, &flags,       sizeof(u32));
    network_send(&p);
}

void network_receive_save_set_flag(struct Packet* p) {
    s32 fileIndex;
    s32 courseIndex;
    u8 courseStars;
    u32 flags;
    packet_read(p, &fileIndex,   sizeof(s32));
    packet_read(p, &courseIndex, sizeof(s32));
    packet_read(p, &courseStars, sizeof(u8));
    packet_read(p, &flags,       sizeof(u32));

    if (fileIndex >= NUM_SAVE_FILES) {
        LOG_ERROR("Invalid fileIndex: %d", fileIndex);
        return;
    }

    if (courseIndex >= COURSE_COUNT) {
        LOG_ERROR("Invalid courseIndex: %d", courseIndex);
        return;
    }

    gSaveBuffer.files[fileIndex][0].courseStars[courseIndex] |= courseStars;
    gSaveBuffer.files[fileIndex][0].flags |= flags;
    gSaveFileModified = TRUE;
}

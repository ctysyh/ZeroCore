#include "zerocore.h"

inline void zc_cleaner_send_message_to_writer(
    zc_writer_id_t writer_id,
    zc_message_type_t type,
    uint16_t length,
    char content[],
    zc_time_t create_time
);
// The efd rewrites (include/rs_efd.h) behind efd_api.
#ifndef RS_GAME_EFD_REWRITE_H
#define RS_GAME_EFD_REWRITE_H

#include "efd_cases.h"

static const efd_api efd_rewrite_api = {
    rsg_efd_id_set_type, rsg_efd_id_set_usage, rsg_efd_id_set_shard, rsg_efd_id_set_net_id,
    rsg_efd_id_set_base_id, rsg_efd_category_init_u0, rsg_efd_category_init_u2, rsg_efd_category_init_svc,
    rsg_efd_fixedstring_init, rsg_efd_fixedstring_init_cstr, rsg_efd_fixedstring_init_copy,
    rsg_efd_fixedstring_release, rsg_efd_fixedstring_assign, rsg_efd_fixedstring_c_str,
    rsg_efd_fixedstring_exists, rsg_efd_fixedstring_length, rsg_efd_fixedstring_equals,
    rsg_efd_string_table_count, rsg_efd_string_table_collisions, rsg_efd_string_table_bucket_size,
    rsg_efd_datastream_init, rsg_efd_datastream_init_copy, rsg_efd_datastream_destroy, rsg_efd_datastream_read,
    rsg_efd_datastream_write, rsg_efd_datastream_seek, rsg_efd_datastream_write_stream,
    rsg_efd_datastream_read_stream, rsg_efd_datastream_write_id, rsg_efd_datastream_read_id,
    rsg_efd_utf8_units_at, rsg_efd_utf8_units, rsg_efd_utf8_tail_missing, rsg_efd_utf8string_length,
    rsg_efd_utf8string_to_lower, rsg_efd_utf8string_to_upper, rsg_efd_path_standardize, rsg_efd_snprintf,
    rsg_efd_fill_sqrt_table, rsg_efd_msghelper_init, rsg_efd_msghelper_on_preinit, rsg_efd_msghelper_on_shutdown,
    rsg_efd_msghelper_on_net_id, rsg_efd_msghelper_on_init, rsg_efd_msghelper_on_tick,
    rsg_efd_msghelper_display_name,
};

// Sets up the rewrites' side: fakes, log hook, string table.
static void efd_rewrite_start(void) {
  efd_fakes_init();
  rsg_efd_set_log(efd_log_line);
  rsg_efd_string_table_create();
}

#endif

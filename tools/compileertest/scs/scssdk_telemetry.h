#pragma once
#include "scssdk.h"
enum { SCS_VALUE_TYPE_INVALID=0, SCS_VALUE_TYPE_bool, SCS_VALUE_TYPE_s32, SCS_VALUE_TYPE_u32,
       SCS_VALUE_TYPE_u64, SCS_VALUE_TYPE_s64, SCS_VALUE_TYPE_float, SCS_VALUE_TYPE_double,
       SCS_VALUE_TYPE_string };
enum { SCS_TELEMETRY_CHANNEL_FLAG_none = 0 };
enum { SCS_TELEMETRY_EVENT_configuration=1, SCS_TELEMETRY_EVENT_gameplay,
       SCS_TELEMETRY_EVENT_paused, SCS_TELEMETRY_EVENT_started };
struct scs_value_bool_t { unsigned char value; };
struct scs_value_s32_t { int value; };
struct scs_value_u32_t { unsigned int value; };
struct scs_value_s64_t { long long value; };
struct scs_value_u64_t { unsigned long long value; };
struct scs_value_float_t { float value; };
struct scs_value_double_t { double value; };
struct scs_value_string_t { const char* value; };
struct scs_value_t {
    int type;
    scs_value_bool_t value_bool; scs_value_s32_t value_s32; scs_value_u32_t value_u32;
    scs_value_s64_t value_s64; scs_value_u64_t value_u64; scs_value_float_t value_float;
    scs_value_double_t value_double; scs_value_string_t value_string;
};
struct scs_named_value_t { const char* name; scs_u32_t index; scs_value_t value; };
struct scs_telemetry_configuration_t { const char* id; const scs_named_value_t* attributes; };
struct scs_telemetry_gameplay_event_t { const char* id; const scs_named_value_t* attributes; };
typedef void (*scs_telemetry_channel_callback_t)(const scs_string_t, scs_u32_t, const scs_value_t*, scs_context_t);
typedef void (*scs_telemetry_event_callback_t)(const scs_event_t, const void*, scs_context_t);
struct scs_telemetry_common_init_params_t {
    void (*log)(int, const char*);
    scs_string_t game_name;   // real header: scs_string_t game_name/game_id, scs_u32_t game_version
    scs_string_t game_id;
    scs_u32_t game_version;
};
struct scs_telemetry_init_params_v101_t {
    scs_telemetry_common_init_params_t common;
    scs_result_t (*register_for_channel)(scs_string_t, scs_u32_t, int, int, scs_telemetry_channel_callback_t, scs_context_t);
    scs_result_t (*register_for_event)(scs_event_t, scs_telemetry_event_callback_t, scs_context_t);
};

// Aanvullingen voor Plugin.cxx (telemetrie-entrypoints).
#define SCS_TELEMETRY_VERSION_1_01 1
#define SCS_RESULT_ok 0
#define SCS_RESULT_unsupported (-1)
#define SCS_LOG_TYPE_message 0
typedef scs_telemetry_init_params_v101_t scs_telemetry_init_params_t;

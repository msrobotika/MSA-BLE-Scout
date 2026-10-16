#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "adv_parser.h"
#define DEVICE_CAP 24
#define SERVICE_CAP 12
#define CHAR_CAP 40
#define PAGE_COUNT 5
typedef struct {uint8_t addr[6],addr_type;int8_t rssi;bool connectable;uint8_t raw[31],raw_len;adv_fields_t adv;uint32_t first,last,reports;} device_t;
typedef struct {char uuid[37];uint16_t start,end;} service_t;
typedef struct {char uuid[37];uint16_t handle;uint8_t properties,service;int read_status;uint16_t read_length;uint8_t value[32],value_len;bool read_tested,encrypted;} characteristic_t;
typedef struct {
 bool synced,scanning,scan_enabled,connected,busy,truncated;
 uint32_t reports,malformed,dropped,uptime,heap,connection_attempts,read_attempts;
 uint8_t count,selected,service_count,char_count,selected_char;
 char phase[40],target[18];int last_error;
 device_t devices[DEVICE_CAP];service_t services[SERVICE_CAP];characteristic_t chars[CHAR_CAP];
} scout_snapshot_t;
void scout_start(void);
void scout_snapshot(scout_snapshot_t *out);
bool scout_select(unsigned id);
void scout_next_device(void);
void scout_next_characteristic(void);
bool scout_scan(bool enabled);
bool scout_clear(void);
bool scout_connect(unsigned id);
bool scout_read(unsigned index);
bool scout_disconnect(void);
void scout_address(const device_t *d,char out[18]);
void serial_blob(const uint8_t *data,unsigned bytes,int width,int height);
void screen_start(int profile,bool invert);
void screen_page(int page);
void screen_profile(int profile,bool invert);
bool screen_capture(void);
unsigned screen_frames(void);
unsigned screen_button_presses(void);
unsigned screen_button_holds(void);
int screen_button_level(void);
int screen_current_page(void);
int screen_current_profile(void);
bool screen_current_invert(void);

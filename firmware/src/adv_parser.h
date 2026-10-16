#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {char name[32],uuid128[37];uint16_t company,uuid16[8];uint8_t uuid_count,flags;bool has_company,has_flags;} adv_fields_t;
bool adv_parse(const uint8_t *data,size_t size,adv_fields_t *out);

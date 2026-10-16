#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "scout.h"
static SemaphoreHandle_t tx_lock;
static uint8_t mac[6];
static void send_json(cJSON *j){char *line=cJSON_PrintUnformatted(j);if(!line)return;xSemaphoreTake(tx_lock,portMAX_DELAY);uart_write_bytes(0,line,strlen(line));uart_write_bytes(0,"\n",1);xSemaphoreGive(tx_lock);free(line);}
static void answer(const char *result){cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"result",result);send_json(j);cJSON_Delete(j);}
void serial_blob(const uint8_t *data,unsigned bytes,int width,int height){
 char header[128];int n=snprintf(header,sizeof(header),"{\"result\":\"capture_begin\",\"bytes\":%u,\"width\":%d,\"height\":%d}\n",bytes,width,height);
 const char *end="{\"result\":\"capture_end\"}\n";xSemaphoreTake(tx_lock,portMAX_DELAY);uart_write_bytes(0,header,n);uart_write_bytes(0,data,bytes);uart_write_bytes(0,end,strlen(end));uart_wait_tx_done(0,pdMS_TO_TICKS(15000));xSemaphoreGive(tx_lock);
}
static void hex(const uint8_t *data,unsigned size,char *out){const char *alphabet="0123456789abcdef";for(unsigned i=0;i<size;i++){out[i*2]=alphabet[data[i]>>4];out[i*2+1]=alphabet[data[i]&15];}out[size*2]=0;}
static void adv_json(cJSON *j,const adv_fields_t *a){
 cJSON_AddStringToObject(j,"name",a->name);if(a->has_company)cJSON_AddNumberToObject(j,"company_id",a->company);if(a->has_flags)cJSON_AddNumberToObject(j,"flags",a->flags);cJSON_AddStringToObject(j,"service_uuid128",a->uuid128);
 cJSON *uuids=cJSON_AddArrayToObject(j,"service_uuids16");for(unsigned i=0;i<a->uuid_count;i++)cJSON_AddItemToArray(uuids,cJSON_CreateNumber(a->uuid16[i]));
}
static cJSON *device_json(unsigned id,const device_t *d,uint32_t now){
 cJSON *j=cJSON_CreateObject();char addr[18],raw[63];scout_address(d,addr);hex(d->raw,d->raw_len,raw);
 cJSON_AddStringToObject(j,"record","device");cJSON_AddNumberToObject(j,"id",id);cJSON_AddStringToObject(j,"address",addr);cJSON_AddNumberToObject(j,"address_type",d->addr_type);cJSON_AddNumberToObject(j,"rssi",d->rssi);cJSON_AddBoolToObject(j,"connectable",d->connectable);cJSON_AddNumberToObject(j,"reports",d->reports);cJSON_AddNumberToObject(j,"age_seconds",now-d->last);cJSON_AddStringToObject(j,"raw_adv_hex",raw);adv_json(j,&d->adv);return j;
}
static cJSON *char_json(unsigned id,const characteristic_t *c){
 cJSON *j=cJSON_CreateObject();char value[65];hex(c->value,c->value_len,value);cJSON_AddStringToObject(j,"record","characteristic");cJSON_AddNumberToObject(j,"index",id);cJSON_AddStringToObject(j,"uuid",c->uuid);cJSON_AddNumberToObject(j,"handle",c->handle);cJSON_AddNumberToObject(j,"service_index",c->service);cJSON_AddNumberToObject(j,"properties",c->properties);cJSON_AddBoolToObject(j,"read_tested",c->read_tested);cJSON_AddNumberToObject(j,"read_status",c->read_status);cJSON_AddBoolToObject(j,"encrypted",c->encrypted);cJSON_AddNumberToObject(j,"read_length",c->read_length);cJSON_AddStringToObject(j,"value_hex",value);return j;
}
static void status(const scout_snapshot_t *s){
 cJSON *j=cJSON_CreateObject();char m[18];snprintf(m,sizeof(m),"%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);cJSON_AddStringToObject(j,"result","status");cJSON_AddStringToObject(j,"firmware","MSA-BLE-Scout-0.1");cJSON_AddStringToObject(j,"mac",m);
 cJSON_AddBoolToObject(j,"ble_ready",s->synced);cJSON_AddBoolToObject(j,"scanning",s->scanning);cJSON_AddBoolToObject(j,"scan_enabled",s->scan_enabled);cJSON_AddBoolToObject(j,"connected",s->connected);cJSON_AddBoolToObject(j,"busy",s->busy);cJSON_AddBoolToObject(j,"passive_scan",true);cJSON_AddBoolToObject(j,"writes_enabled",false);cJSON_AddBoolToObject(j,"pairing_enabled",false);cJSON_AddBoolToObject(j,"wifi_enabled",false);
 cJSON_AddStringToObject(j,"phase",s->phase);cJSON_AddStringToObject(j,"target",s->target);cJSON_AddNumberToObject(j,"last_error",s->last_error);cJSON_AddNumberToObject(j,"devices",s->count);cJSON_AddNumberToObject(j,"selected",s->selected);cJSON_AddNumberToObject(j,"selected_characteristic",s->selected_char);cJSON_AddNumberToObject(j,"services",s->service_count);cJSON_AddNumberToObject(j,"characteristics",s->char_count);cJSON_AddBoolToObject(j,"gatt_truncated",s->truncated);
 cJSON_AddNumberToObject(j,"reports",s->reports);cJSON_AddNumberToObject(j,"malformed",s->malformed);cJSON_AddNumberToObject(j,"capacity_drops",s->dropped);cJSON_AddNumberToObject(j,"uptime",s->uptime);cJSON_AddNumberToObject(j,"heap",s->heap);cJSON_AddNumberToObject(j,"connection_attempts",s->connection_attempts);cJSON_AddNumberToObject(j,"read_attempts",s->read_attempts);
 cJSON_AddNumberToObject(j,"screen_frames",screen_frames());cJSON_AddNumberToObject(j,"page",screen_current_page());cJSON_AddNumberToObject(j,"display_profile",screen_current_profile());cJSON_AddBoolToObject(j,"display_invert",screen_current_invert());cJSON_AddNumberToObject(j,"button_level",screen_button_level());cJSON_AddNumberToObject(j,"button_presses",screen_button_presses());cJSON_AddNumberToObject(j,"button_holds",screen_button_holds());
 if(s->count)cJSON_AddItemToObject(j,"selected_device",device_json(s->selected,&s->devices[s->selected],s->uptime));
 if(s->char_count)cJSON_AddItemToObject(j,"selected_attribute",char_json(s->selected_char,&s->chars[s->selected_char]));
 send_json(j);cJSON_Delete(j);
}
static void export_records(const scout_snapshot_t *s,bool devices_only){
 answer(devices_only?"devices_begin":"export_begin");
 for(unsigned i=0;i<s->count;i++){cJSON *j=device_json(i,&s->devices[i],s->uptime);send_json(j);cJSON_Delete(j);}
 if(!devices_only){
  for(unsigned i=0;i<s->service_count;i++){cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"record","service");cJSON_AddNumberToObject(j,"index",i);cJSON_AddStringToObject(j,"uuid",s->services[i].uuid);cJSON_AddNumberToObject(j,"start",s->services[i].start);cJSON_AddNumberToObject(j,"end",s->services[i].end);send_json(j);cJSON_Delete(j);}
  for(unsigned i=0;i<s->char_count;i++){cJSON *j=char_json(i,&s->chars[i]);send_json(j);cJSON_Delete(j);}
 }
 answer(devices_only?"devices_end":"export_end");
}
static int number(cJSON *j,const char *name){cJSON *v=cJSON_GetObjectItem(j,name);return cJSON_IsNumber(v)&&v->valuedouble==v->valueint?v->valueint:-1;}
static bool confirmed(cJSON *j){return cJSON_IsTrue(cJSON_GetObjectItem(j,"confirmed"));}
static void save_display(int profile,bool invert){nvs_handle_t h;if(nvs_open_from_partition("scout_nvs","scout",NVS_READWRITE,&h)==ESP_OK){nvs_set_u8(h,"profile",profile);nvs_set_u8(h,"invert",invert);nvs_commit(h);nvs_close(h);}}
static void parse_test(cJSON *j){
 cJSON *value=cJSON_GetObjectItem(j,"hex");if(!cJSON_IsString(value)||strlen(value->valuestring)>64||strlen(value->valuestring)%2){answer("invalid_hex");return;}
 uint8_t bytes[32];unsigned len=strlen(value->valuestring)/2;
 for(unsigned i=0;i<len;i++){char a=value->valuestring[i*2],b=value->valuestring[i*2+1];if(!isxdigit((unsigned char)a)||!isxdigit((unsigned char)b)){answer("invalid_hex");return;}a=tolower((unsigned char)a);b=tolower((unsigned char)b);bytes[i]=((a<='9'?a-'0':a-'a'+10)<<4)|(b<='9'?b-'0':b-'a'+10);}
 adv_fields_t adv;bool valid=adv_parse(bytes,len,&adv);cJSON *out=cJSON_CreateObject();cJSON_AddStringToObject(out,"result","parse_test");cJSON_AddBoolToObject(out,"valid",valid);if(valid)adv_json(out,&adv);send_json(out);cJSON_Delete(out);
}
static void handle(char *line){
 cJSON *j=cJSON_Parse(line);cJSON *command=cJSON_GetObjectItem(j,"cmd");if(!cJSON_IsObject(j)||!cJSON_IsString(command)){cJSON_Delete(j);answer("invalid_command");return;}
 const char *cmd=command->valuestring;
 if(!strcmp(cmd,"status")||!strcmp(cmd,"devices")||!strcmp(cmd,"export")){
  scout_snapshot_t *s=malloc(sizeof(*s));if(!s)answer("low_memory");else{scout_snapshot(s);if(!strcmp(cmd,"status"))status(s);else export_records(s,!strcmp(cmd,"devices"));free(s);}
 }else if(!strcmp(cmd,"select"))answer(scout_select(number(j,"id"))?"selected":"invalid_device");
 else if(!strcmp(cmd,"clear"))answer(scout_clear()?"cache_cleared":"busy");
 else if(!strcmp(cmd,"scan")){cJSON *v=cJSON_GetObjectItem(j,"enabled");answer(cJSON_IsBool(v)&&scout_scan(cJSON_IsTrue(v))?"scan_changed":"invalid_scan");}
 else if(!strcmp(cmd,"connect"))answer(!confirmed(j)?"confirmation_required":scout_connect(number(j,"id"))?"connect_started":"connection_rejected");
 else if(!strcmp(cmd,"read"))answer(!confirmed(j)?"confirmation_required":scout_read(number(j,"index"))?"read_started":"read_rejected");
 else if(!strcmp(cmd,"disconnect"))answer(scout_disconnect()?"disconnect_requested":"busy");
 else if(!strcmp(cmd,"parse_test"))parse_test(j);
 else if(!strcmp(cmd,"page")){int page=number(j,"index");if(page>=0&&page<PAGE_COUNT){screen_page(page);answer("page_set");}else answer("invalid_page");}
 else if(!strcmp(cmd,"display")){int p=number(j,"profile");cJSON *i=cJSON_GetObjectItem(j,"invert");if(p>=0&&p<2&&cJSON_IsBool(i)){screen_profile(p,cJSON_IsTrue(i));save_display(p,cJSON_IsTrue(i));answer("display_set");}else answer("invalid_display");}
 else if(!strcmp(cmd,"capture")){if(!screen_capture())answer("capture_failed");}
 else answer("unsupported_command");
 cJSON_Delete(j);
}
void app_main(void){
 tx_lock=xSemaphoreCreateMutex();uart_config_t uart={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};ESP_ERROR_CHECK(uart_param_config(0,&uart));ESP_ERROR_CHECK(uart_driver_install(0,2048,4096,0,NULL,0));esp_read_mac(mac,ESP_MAC_WIFI_STA);
 esp_err_t e=nvs_flash_init_partition("scout_nvs");if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase_partition("scout_nvs"));e=nvs_flash_init_partition("scout_nvs");}ESP_ERROR_CHECK(e);
 int profile=0;bool invert=true;nvs_handle_t h;if(nvs_open_from_partition("scout_nvs","scout",NVS_READONLY,&h)==ESP_OK){uint8_t v;if(nvs_get_u8(h,"profile",&v)==ESP_OK&&v<2)profile=v;if(nvs_get_u8(h,"invert",&v)==ESP_OK)invert=v!=0;nvs_close(h);}
 screen_start(profile,invert);scout_start();answer("ready");
 char line[512];unsigned used=0;bool discard=false;uint8_t buf[128];while(1){int n=uart_read_bytes(0,buf,sizeof(buf),pdMS_TO_TICKS(100));for(int i=0;i<n;i++){
  if(buf[i]=='\n'){if(!discard&&used){line[used]=0;handle(line);}used=0;discard=false;}else if(!discard){if(used<sizeof(line)-1)line[used++]=buf[i];else{discard=true;used=0;}}
 }}
}

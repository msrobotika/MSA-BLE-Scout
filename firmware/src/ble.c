#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "scout.h"

static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static scout_snapshot_t state;
static QueueHandle_t commands;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static uint8_t own_type;
static unsigned discover_service;
static int64_t operation_started;
typedef struct {int kind,index;device_t target;} request_t;
static int gap_event(struct ble_gap_event *event,void *arg);
static uint32_t seconds(void){return (uint32_t)(esp_timer_get_time()/1000000);}
void scout_address(const device_t *d,char out[18]){snprintf(out,18,"%02x:%02x:%02x:%02x:%02x:%02x",d->addr[5],d->addr[4],d->addr[3],d->addr[2],d->addr[1],d->addr[0]);}
static void phase(const char *label,int error,bool busy){portENTER_CRITICAL(&mux);strlcpy(state.phase,label,sizeof(state.phase));state.last_error=error;state.busy=busy;portEXIT_CRITICAL(&mux);}
void scout_snapshot(scout_snapshot_t *out){portENTER_CRITICAL(&mux);*out=state;portEXIT_CRITICAL(&mux);out->uptime=seconds();out->heap=esp_get_free_heap_size();}
static void advertisement(const struct ble_gap_disc_desc *d){
 adv_fields_t adv;bool valid=adv_parse(d->data,d->length_data,&adv);uint32_t now=seconds();
 portENTER_CRITICAL(&mux);state.reports++;
 if(!valid){state.malformed++;portEXIT_CRITICAL(&mux);return;}
 unsigned index=state.count;
 for(unsigned i=0;i<state.count;i++)if(state.devices[i].addr_type==d->addr.type&&!memcmp(state.devices[i].addr,d->addr.val,6)){index=i;break;}
 if(index==state.count){if(state.count==DEVICE_CAP){state.dropped++;portEXIT_CRITICAL(&mux);return;}state.count++;state.devices[index].first=now;memcpy(state.devices[index].addr,d->addr.val,6);state.devices[index].addr_type=d->addr.type;}
 device_t *out=&state.devices[index];out->rssi=d->rssi;out->last=now;out->reports++;out->connectable=d->event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||d->event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
 if(adv.name[0])strlcpy(out->adv.name,adv.name,sizeof(out->adv.name));
 if(adv.has_company){out->adv.has_company=true;out->adv.company=adv.company;}
 if(adv.has_flags){out->adv.has_flags=true;out->adv.flags=adv.flags;}
 if(adv.uuid_count){out->adv.uuid_count=adv.uuid_count;memcpy(out->adv.uuid16,adv.uuid16,sizeof(adv.uuid16));}
 if(adv.uuid128[0])strlcpy(out->adv.uuid128,adv.uuid128,sizeof(out->adv.uuid128));
 out->raw_len=d->length_data;memcpy(out->raw,d->data,d->length_data);
 portEXIT_CRITICAL(&mux);
}
static void scan_start(void){
 struct ble_gap_disc_params params={.itvl=160,.window=80,.passive=1,.filter_duplicates=0};
 int rc=ble_gap_disc(own_type,BLE_HS_FOREVER,&params,gap_event,NULL);
 portENTER_CRITICAL(&mux);state.scanning=rc==0;portEXIT_CRITICAL(&mux);
 if(rc&&rc!=BLE_HS_EALREADY)phase("Error de escaneo",rc,false);
}
static void scan_stop(void){if(ble_gap_disc_active())ble_gap_disc_cancel();portENTER_CRITICAL(&mux);state.scanning=false;portEXIT_CRITICAL(&mux);}
static void next_characteristic_service(void);
static int on_characteristic(uint16_t handle,const struct ble_gatt_error *error,const struct ble_gatt_chr *chr,void *arg){
 if(!error->status){
  portENTER_CRITICAL(&mux);
  if(state.char_count<CHAR_CAP){characteristic_t *c=&state.chars[state.char_count++];ble_uuid_to_str(&chr->uuid.u,c->uuid);c->handle=chr->val_handle;c->properties=chr->properties;c->service=discover_service;c->read_status=-1;}
  else state.truncated=true;
  portEXIT_CRITICAL(&mux);
 }else if(error->status==BLE_HS_EDONE){discover_service++;next_characteristic_service();}
 else phase("Error GATT",error->status,false);
 return 0;
}
static void next_characteristic_service(void){
 if(discover_service>=state.service_count){phase("GATT listo",0,false);return;}
 service_t svc;portENTER_CRITICAL(&mux);svc=state.services[discover_service];portEXIT_CRITICAL(&mux);
 int rc=ble_gattc_disc_all_chrs(connection,svc.start,svc.end,on_characteristic,NULL);if(rc)phase("Error al enumerar",rc,false);
}
static int on_service(uint16_t handle,const struct ble_gatt_error *error,const struct ble_gatt_svc *svc,void *arg){
 if(!error->status){
  portENTER_CRITICAL(&mux);
  if(state.service_count<SERVICE_CAP){service_t *out=&state.services[state.service_count++];out->start=svc->start_handle;out->end=svc->end_handle;ble_uuid_to_str(&svc->uuid.u,out->uuid);}
  else state.truncated=true;
  portEXIT_CRITICAL(&mux);
 }else if(error->status==BLE_HS_EDONE){discover_service=0;next_characteristic_service();}
 else phase("Error de servicios",error->status,false);
 return 0;
}
static int on_read(uint16_t handle,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
 unsigned index=(unsigned)(uintptr_t)arg;struct ble_gap_conn_desc desc;bool encrypted=ble_gap_conn_find(handle,&desc)==0&&desc.sec_state.encrypted;
 portENTER_CRITICAL(&mux);
 if(index<state.char_count){
  characteristic_t *c=&state.chars[index];c->read_tested=true;c->read_status=error->status;c->encrypted=encrypted;c->value_len=0;
  if(!error->status&&attr&&attr->om){c->read_length=OS_MBUF_PKTLEN(attr->om);c->value_len=c->read_length>sizeof(c->value)?sizeof(c->value):c->read_length;os_mbuf_copydata(attr->om,0,c->value_len,c->value);}
 }
 portEXIT_CRITICAL(&mux);phase(error->status?"Lectura rechazada":"Lectura recibida",error->status,false);return 0;
}
static int gap_event(struct ble_gap_event *event,void *arg){
 switch(event->type){
 case BLE_GAP_EVENT_DISC:advertisement(&event->disc);break;
 case BLE_GAP_EVENT_DISC_COMPLETE:portENTER_CRITICAL(&mux);state.scanning=false;portEXIT_CRITICAL(&mux);break;
 case BLE_GAP_EVENT_CONNECT:
  if(event->connect.status){connection=BLE_HS_CONN_HANDLE_NONE;phase("Conexion fallida",event->connect.status,false);}
  else {
   connection=event->connect.conn_handle;portENTER_CRITICAL(&mux);state.connected=true;portEXIT_CRITICAL(&mux);phase("Enumerando GATT",0,true);operation_started=esp_timer_get_time();
   int rc=ble_gattc_disc_all_svcs(connection,on_service,NULL);if(rc)phase("Error GATT",rc,false);
  }break;
 case BLE_GAP_EVENT_DISCONNECT:
  connection=BLE_HS_CONN_HANDLE_NONE;portENTER_CRITICAL(&mux);state.connected=false;portEXIT_CRITICAL(&mux);phase("Desconectado",event->disconnect.reason,false);break;
 case BLE_GAP_EVENT_PASSKEY_ACTION:
  // No pairing, bonding, passkey guessing or automatic security escalation.
  ble_gap_terminate(connection,BLE_ERR_AUTH_FAIL);phase("Emparejamiento omitido",0,false);break;
 default:break;
 }
 return 0;
}
static void on_sync(void){int rc=ble_hs_util_ensure_addr(0);if(!rc)rc=ble_hs_id_infer_auto(0,&own_type);portENTER_CRITICAL(&mux);state.synced=rc==0;portEXIT_CRITICAL(&mux);phase(rc?"Error BLE":"Escaneo pasivo",rc,false);}
static void on_reset(int reason){portENTER_CRITICAL(&mux);state.synced=false;state.connected=false;state.scanning=false;portEXIT_CRITICAL(&mux);connection=BLE_HS_CONN_HANDLE_NONE;phase("Reiniciando BLE",reason,false);}
static void host_task(void *unused){nimble_port_run();nimble_port_freertos_deinit();}
static void control_task(void *unused){
 request_t command;
 while(1){
  if(xQueueReceive(commands,&command,pdMS_TO_TICKS(100))==pdTRUE){
   if(command.kind==1){
    scan_stop();ble_addr_t addr={.type=command.target.addr_type};memcpy(addr.val,command.target.addr,6);operation_started=esp_timer_get_time();
    int rc=ble_gap_connect(own_type,&addr,10000,NULL,gap_event,NULL);if(rc)phase("No se pudo conectar",rc,false);
   }else if(command.kind==2){
    operation_started=esp_timer_get_time();uint16_t handle;portENTER_CRITICAL(&mux);handle=state.chars[command.index].handle;portEXIT_CRITICAL(&mux);
    int rc=ble_gattc_read(connection,handle,on_read,(void*)(uintptr_t)command.index);if(rc)phase("No se pudo leer",rc,false);
   }else if(command.kind==3){
    scan_stop();if(connection!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);else ble_gap_conn_cancel();
   }else if(command.kind==4){if(!command.index)scan_stop();}
  }
  bool ready,enabled,busy,scanning,linked;portENTER_CRITICAL(&mux);ready=state.synced;enabled=state.scan_enabled;busy=state.busy;scanning=state.scanning;linked=state.connected;portEXIT_CRITICAL(&mux);
  if(busy&&esp_timer_get_time()-operation_started>18000000){if(connection!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);else ble_gap_conn_cancel();phase("Tiempo agotado",BLE_HS_ETIMEOUT,false);}
  if(ready&&enabled&&!busy&&!scanning&&!linked)scan_start();
 }
}
void scout_start(void){
 strcpy(state.phase,"Iniciando BLE");state.scan_enabled=true;commands=xQueueCreate(4,sizeof(request_t));assert(commands);
 int rc=nimble_port_init();if(rc){phase("Sin memoria BLE",rc,false);return;}
 ble_hs_cfg.reset_cb=on_reset;ble_hs_cfg.sync_cb=on_sync;
 // Never initiate security/pairing and never persist bonds.
#if MYNEWT_VAL(BLE_SM)
 ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_sc=0;
#endif
 nimble_port_freertos_init(host_task);assert(xTaskCreate(control_task,"scout_control",4096,NULL,4,NULL)==pdPASS);
}
bool scout_select(unsigned id){bool valid;portENTER_CRITICAL(&mux);valid=id<state.count;if(valid)state.selected=id;portEXIT_CRITICAL(&mux);return valid;}
void scout_next_device(void){portENTER_CRITICAL(&mux);if(state.count)state.selected=(state.selected+1)%state.count;portEXIT_CRITICAL(&mux);}
void scout_next_characteristic(void){portENTER_CRITICAL(&mux);if(state.char_count)state.selected_char=(state.selected_char+1)%state.char_count;portEXIT_CRITICAL(&mux);}
bool scout_scan(bool enabled){portENTER_CRITICAL(&mux);state.scan_enabled=enabled;portEXIT_CRITICAL(&mux);request_t req={.kind=4,.index=enabled};return commands&&xQueueSend(commands,&req,0)==pdTRUE;}
bool scout_clear(void){bool allowed;portENTER_CRITICAL(&mux);allowed=!state.connected&&!state.busy;if(allowed){memset(state.devices,0,sizeof(state.devices));state.count=0;state.selected=0;state.dropped=0;}portEXIT_CRITICAL(&mux);return allowed;}
bool scout_connect(unsigned id){
 request_t req={.kind=1,.index=id};bool valid;
 portENTER_CRITICAL(&mux);valid=state.synced&&id<state.count&&state.devices[id].connectable&&!state.connected&&!state.busy;
 if(valid){req.target=state.devices[id];state.selected=id;state.busy=true;state.connection_attempts++;state.service_count=0;state.char_count=0;state.selected_char=0;state.truncated=false;memset(state.chars,0,sizeof(state.chars));scout_address(&req.target,state.target);strcpy(state.phase,"Conectando...");}
 portEXIT_CRITICAL(&mux);
 if(!valid)return false;
 if(xQueueSend(commands,&req,0)!=pdTRUE){phase("Cola ocupada",BLE_HS_EBUSY,false);return false;}return true;
}
bool scout_read(unsigned index){
 bool valid;request_t req={.kind=2,.index=index};portENTER_CRITICAL(&mux);valid=state.connected&&!state.busy&&index<state.char_count&&(state.chars[index].properties&BLE_GATT_CHR_PROP_READ);
 if(valid){state.selected_char=index;state.busy=true;state.read_attempts++;strcpy(state.phase,"Leyendo atributo...");}portEXIT_CRITICAL(&mux);
 if(!valid)return false;
 if(xQueueSend(commands,&req,0)!=pdTRUE){phase("Cola ocupada",BLE_HS_EBUSY,false);return false;}
 return true;
}
bool scout_disconnect(void){request_t req={.kind=3};return commands&&xQueueSend(commands,&req,0)==pdTRUE;}

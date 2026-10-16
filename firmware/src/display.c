#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "ui_fonts.h"
#include "scout.h"
#define BG 0x070B17
#define CARD 0x111D32
#define EDGE 0x26364E
#define WHITE 0xE8F3FF
#define DIM 0x8397B3
#define TEAL 0x43E9CA
#define VIOLET 0xB08AFF
#define AMBER 0xFFC073
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t flushed,capture_done;
static uint16_t *pixels;
static int width=170,height=320;
static volatile int page,requested_profile,applied_profile=-1;
static volatile bool requested_invert=true,applied_invert=true,capture_requested;
static volatile unsigned frames,button_presses,button_holds;
static volatile uint32_t manual_until;
static uint32_t last_rotation;
static int64_t frame_ms;
static unsigned confirm_index;
static char confirm_address[18];
static scout_snapshot_t frame_state;
static const char *titles[]={"RADAR PASIVO","DISPOSITIVOS","DETALLE","GATT","EVIDENCIAS","CONFIRMAR CONEXION","CONFIRMAR LECTURA"};
static uint32_t millis(void){return (uint32_t)(esp_timer_get_time()/1000);}
static bool manual_active(uint32_t now){return manual_until&&(int32_t)(manual_until-now)>0;}
static uint16_t rgb565(uint32_t c){return __builtin_bswap16(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void pixel(int x,int y,uint32_t c){if(x>=0&&x<width&&y>=0&&y<height)pixels[y*width+x]=rgb565(c);}
static void rect(int x,int y,int w,int h,uint32_t c){
 int left=x<0?0:x,right=x+w>width?width:x+w,top=y<0?0:y,bottom=y+h>height?height:y+h;
 uint16_t color=rgb565(c);for(int yy=top;yy<bottom;yy++)for(int xx=left;xx<right;xx++)pixels[yy*width+xx]=color;
}
static void line(int x0,int y0,int x1,int y1,uint32_t c){
 int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
 while(1){pixel(x0,y0,c);if(x0==x1&&y0==y1)break;int twice=2*e;if(twice>=dy){e+=dy;x0+=sx;}if(twice<=dx){e+=dx;y0+=sy;}}
}
static void outline(int x,int y,int w,int h,uint32_t c){line(x,y,x+w-1,y,c);line(x,y+h-1,x+w-1,y+h-1,c);line(x,y,x,y+h-1,c);line(x+w-1,y,x+w-1,y+h-1,c);}
static void alpha(int x,int y,uint32_t c,uint8_t a){
 if(!a||x<0||x>=width||y<0||y>=height)return;
 uint16_t old=__builtin_bswap16(pixels[y*width+x]);int r=((old>>11)&31)*255/31,g=((old>>5)&63)*255/63,b=(old&31)*255/31;
 r=(r*(255-a)+((c>>16)&255)*a)/255;g=(g*(255-a)+((c>>8)&255)*a)/255;b=(b*(255-a)+(c&255)*a)/255;pixel(x,y,(r<<16)|(g<<8)|b);
}
static int text_width(const char *s,const ui_font_t *font){int n=0;for(;*s;s++){unsigned ch=(unsigned char)*s;if(ch<32||ch>126)ch='?';n+=font->glyph[ch-32].width;}return n;}
static void text(int x,int y,const char *s,const ui_font_t *font,uint32_t c){
 for(;*s;s++){
  unsigned ch=(unsigned char)*s;if(ch<32||ch>126)ch='?';const glyph_t *g=&font->glyph[ch-32];
  for(int yy=0;yy<font->height;yy++)for(int xx=0;xx<g->width;xx++)alpha(x+xx,y+yy,c,font->data[g->offset+yy*g->width+xx]);
  x+=g->width;if(x>=width)break;
 }
}
static void centered(int y,const char *s,const ui_font_t *font,uint32_t c){text((width-text_width(s,font))/2,y,s,font,c);}
static void clipped(int x,int y,int available,const char *s,const ui_font_t *font,uint32_t c){
 char small[48];strlcpy(small,s,sizeof(small));if(text_width(small,font)>available){size_t n=strlen(small);while(n&&text_width(small,font)+text_width("..",font)>available)small[--n]=0;strlcat(small,"..",sizeof(small));}text(x,y,small,font,c);
}
static void circle(int cx,int cy,int r,uint32_t ink){int x=r,y=0,e=1-r;while(x>=y){pixel(cx+x,cy+y,ink);pixel(cx+y,cy+x,ink);pixel(cx-y,cy+x,ink);pixel(cx-x,cy+y,ink);pixel(cx-x,cy-y,ink);pixel(cx-y,cy-x,ink);pixel(cx+y,cy-x,ink);pixel(cx+x,cy-y,ink);y++;if(e<0)e+=2*y+1;else{x--;e+=2*(y-x)+1;}}}
static void header(const scout_snapshot_t *s){
 text(9,4,"MSA_ROBOTIKA",&font11,DIM);text(8,18,"BLE SCOUT",&font19,WHITE);
 rect(width-15,9,5,5,s->connected?VIOLET:s->scanning?TEAL:AMBER);line(8,46,width-9,46,EDGE);rect(8,46,18+(frame_ms/40)%(width-37),1,VIOLET);
 text(9,51,titles[page],&font11,TEAL);
}
static void footer(void){
 int y=height-36;line(8,y,width-9,y,EDGE);
 if(page<PAGE_COUNT){for(unsigned i=0;i<PAGE_COUNT;i++)rect(width/2-28+i*12,y+4,7,2,i==(unsigned)page?TEAL:EDGE);}
 centered(height-25,page>=5?"1x: cancelar":"1x: vista | 2x: elegir",&font11,DIM);
 const char *action=page==0?"2s: pausa / desconecta":page==1?"2s: abrir detalle":page==2?"2s: solicitar conexion":page==3?"2s: solicitar lectura":page==4?"2s: ver atributo":"2s: confirmar (equipo tuyo)";
 centered(height-12,action,&font11,page>=5?AMBER:DIM);
}
static void radar(const scout_snapshot_t *s){
 int cx=width/2,cy=height>240?139:111,r=width>135?60:43;
 for(int yy=70;yy<cy+r+10;yy+=13)for(int xx=10;xx<width-10;xx+=13)pixel(xx,yy,0x102139);
 circle(cx,cy,r,EDGE);circle(cx,cy,r*2/3,EDGE);circle(cx,cy,r/3,EDGE);line(cx-r,cy,cx+r,cy,EDGE);line(cx,cy-r,cx,cy+r,EDGE);
 float angle=(frame_ms%4000)*6.2831853f/4000;
 if(s->scanning){for(int i=0;i<4;i++){float a=angle-i*.06f;line(cx,cy,cx+cosf(a)*r,cy+sinf(a)*r,i==0?TEAL:0x17473F);}}
 for(unsigned i=0;i<s->count;i++){
  const device_t *d=&s->devices[i];if(s->uptime-d->last>15||d->rssi==127)continue;
  unsigned hash=d->addr[0]*13+d->addr[1]*7+d->addr[2];float a=(hash%360)*.0174533f;int distance=(-d->rssi-25)*r/75;if(distance<8)distance=8;if(distance>r-3)distance=r-3;
  int x=cx+cosf(a)*distance,y=cy+sinf(a)*distance;rect(x-1,y-1,3,3,i==s->selected?WHITE:VIOLET);if(i==s->selected)circle(x,y,4,TEAL);
 }
 circle(cx,cy,3,WHITE);char value[48];int y=height>240?212:162;
 snprintf(value,sizeof(value),"%u",s->count);text(11,y,value,&font26,WHITE);text(54,y+10,"DIRECCIONES",&font11,DIM);
 if(height>240){snprintf(value,sizeof(value),"%lu anuncios recibidos",(unsigned long)s->reports);text(11,251,value,&font11,TEAL);}
 centered(height-53,s->scanning?"PASIVO / RSSI, NO POSICION":s->connected?"SESION GATT ACTIVA":"ESCANEO PAUSADO",&font11,s->scanning?TEAL:AMBER);
}
static void device_list(const scout_snapshot_t *s){
 if(!s->count){centered(102,"BUSCANDO",&font19,TEAL);centered(133,"Solo anuncios BLE visibles",&font11,DIM);return;}
 unsigned rows=height>240?4:2,start=s->selected/rows*rows;char value[48];int step=height>240?47:56;
 for(unsigned row=0;row<rows&&start+row<s->count;row++){
  unsigned id=start+row;const device_t *d=&s->devices[id];int y=73+row*step;rect(8,y,width-16,step-4,CARD);if(id==s->selected){outline(8,y,width-16,step-4,TEAL);rect(8,y,2,step-4,TEAL);}
  snprintf(value,sizeof(value),"#%02u %s",id,d->adv.name[0]?d->adv.name:"Sin nombre");clipped(14,y+1,width-28,value,&font13,WHITE);
  snprintf(value,sizeof(value),"%02X:%02X:%02X  %d dBm",d->addr[2],d->addr[1],d->addr[0],d->rssi);text(14,y+22,value,&font11,s->uptime-d->last>15?DIM:VIOLET);
 }
 if(height>240){snprintf(value,sizeof(value),"Elegido #%02u | limite 24",s->selected);text(10,267,value,&font11,DIM);}
}
static void detail(const scout_snapshot_t *s){
 if(!s->count){centered(112,"SIN DISPOSITIVOS",&font13,DIM);return;}
 const device_t *d=&s->devices[s->selected];char addr[18],value[48];scout_address(d,addr);clipped(9,73,width-18,d->adv.name[0]?d->adv.name:"Sin nombre anunciado",&font13,WHITE);text(9,95,addr,&font11,TEAL);
 snprintf(value,sizeof(value),"%d dBm | %s",d->rssi,d->addr_type?"aleatoria":"publica");text(9,117,value,&font11,DIM);
 text(9,140,d->connectable?"CONECTABLE":"NO CONECTABLE",&font13,d->connectable?TEAL:AMBER);
 snprintf(value,sizeof(value),"Fabricante ID: %04X",d->adv.company);text(9,163,d->adv.has_company?value:"Sin fabricante anunciado",&font11,DIM);
 if(height>240){snprintf(value,sizeof(value),"Visto hace %lus",(unsigned long)(s->uptime-d->last));text(9,190,value,&font11,DIM);snprintf(value,sizeof(value),"%lu anuncios de esta direccion",(unsigned long)d->reports);clipped(9,211,width-18,value,&font11,DIM);text(9,242,"Nombre y MAC no prueban identidad",&font11,DIM);text(9,260,"Conecta solo equipos tuyos",&font11,AMBER);}
}
static void short_uuid(const char *uuid,char out[20]){if(strlen(uuid)>18)snprintf(out,20,"%.8s..%.4s",uuid,uuid+strlen(uuid)-4);else strlcpy(out,uuid,20);}
static void gatt_view(const scout_snapshot_t *s){
 char value[48],uuid[20];clipped(9,72,width-18,s->phase,&font13,s->connected?TEAL:DIM);
 snprintf(value,sizeof(value),"%u servicios / %u atributos",s->service_count,s->char_count);text(9,96,value,&font11,DIM);
 if(!s->char_count){text(9,126,"Selecciona un equipo",&font13,WHITE);text(9,147,"en DISPOSITIVOS.",&font13,WHITE);text(9,178,"Sin conexiones automaticas",&font11,TEAL);return;}
 const characteristic_t *c=&s->chars[s->selected_char];rect(8,121,width-16,height>240?105:70,CARD);snprintf(value,sizeof(value),"ATRIBUTO %u / %u",s->selected_char+1,s->char_count);text(15,127,value,&font11,VIOLET);short_uuid(c->uuid,uuid);text(15,146,uuid,&font13,WHITE);
 snprintf(value,sizeof(value),"R:%s W:%s N:%s",c->properties&2?"SI":"--",c->properties&12?"SI":"--",c->properties&16?"SI":"--");text(15,171,value,&font11,TEAL);
 if(height>240){snprintf(value,sizeof(value),"Handle: %04X",c->handle);text(15,196,value,&font11,DIM);text(10,243,"R/W son propiedades declaradas",&font11,DIM);text(10,261,"No equivalen a permisos reales",&font11,AMBER);}
}
static void evidence(const scout_snapshot_t *s){
 char value[48];unsigned writable=0,read_ok=0,denied=0;
 for(unsigned i=0;i<s->char_count;i++){const characteristic_t *c=&s->chars[i];if(c->properties&12)writable++;if(c->read_tested){if(!c->read_status)read_ok++;else denied++;}}
 text(10,75,"LECTURAS SOLICITADAS",&font11,DIM);snprintf(value,sizeof(value),"%u OK / %u rechazadas",read_ok,denied);text(10,94,value,&font13,WHITE);
 snprintf(value,sizeof(value),"%u atributos declaran W",writable);text(10,122,value,&font11,VIOLET);
 text(10,145,"ESCRITURAS BLOQUEADAS",&font11,TEAL);
 if(s->char_count){const characteristic_t *c=&s->chars[s->selected_char];
  const char *verdict=!c->read_tested?"SIN PRUEBA ACTIVA":c->read_status?"LECTURA RECHAZADA":c->encrypted?"LECTURA CIFRADA":"LECTURA SIN CIFRAR";
  rect(8,170,width-16,height>240?54:28,CARD);text(13,175,verdict,&font11,c->read_tested&&!c->read_status&&!c->encrypted?AMBER:TEAL);
  if(height>240&&c->read_tested){snprintf(value,sizeof(value),"Estado: %d | %u bytes",c->read_status,c->read_length);text(13,197,value,&font11,DIM);}
 }
 if(height>240){text(10,244,"Un permiso abierto puede ser normal",&font11,DIM);text(10,262,"No declara vulnerabilidades",&font11,DIM);}
}
static void confirmation(const scout_snapshot_t *s){
 centered(81,page==5?"CONECTAR":"LEER ATRIBUTO",&font19,AMBER);centered(117,"Solo equipos tuyos",&font13,WHITE);centered(141,confirm_address,&font11,TEAL);
 if(height>240){centered(178,"Sin emparejar ni escribir",&font11,DIM);centered(202,"La lectura puede devolver datos",&font11,DIM);centered(237,"Manten BOOT 2 segundos",&font13,AMBER);}
 else centered(175,"Manten BOOT 2 segundos",&font11,AMBER);
}
static void apply_geometry(void){if(applied_profile==requested_profile&&applied_invert==requested_invert)return;applied_profile=requested_profile;applied_invert=requested_invert;width=applied_profile==0?170:135;height=applied_profile==0?320:240;esp_lcd_panel_swap_xy(panel,false);esp_lcd_panel_mirror(panel,false,false);esp_lcd_panel_set_gap(panel,applied_profile==0?35:52,applied_profile==0?0:40);esp_lcd_panel_invert_color(panel,applied_invert);}
static bool color_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *ctx){BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(flushed,&wake);return wake==pdTRUE;}
static void single_press(void){page=page>=5?(page==5?2:3):(page+1)%PAGE_COUNT;manual_until=millis()+60000;}
static void double_press(void){if(page==1||page==2)scout_next_device();else if(page==3||page==4)scout_next_characteristic();else if(page==0)scout_clear();manual_until=millis()+60000;}
static void hold_press(void){
 scout_snapshot_t *s=malloc(sizeof(*s));if(!s)return;scout_snapshot(s);manual_until=millis()+60000;
 if(page==0){if(s->connected||s->busy)scout_disconnect();else scout_scan(!s->scan_enabled);}
 else if(page==1)page=2;
 else if(page==2){if(s->connected)scout_disconnect();else if(s->count&&s->devices[s->selected].connectable){confirm_index=s->selected;scout_address(&s->devices[confirm_index],confirm_address);page=5;}}
 else if(page==3){if(s->connected&&!s->busy&&s->char_count&&(s->chars[s->selected_char].properties&2)){confirm_index=s->selected_char;strlcpy(confirm_address,s->target,sizeof(confirm_address));page=6;}}
 else if(page==4)page=3;
 else if(page==5){char current[18];if(confirm_index<s->count){scout_address(&s->devices[confirm_index],current);if(!strcmp(current,confirm_address))scout_connect(confirm_index);}page=3;}
 else if(page==6){if(!strcmp(s->target,confirm_address))scout_read(confirm_index);page=4;}
 free(s);
}
static void button_task(void *unused){
 bool raw=false,stable=false,acted=false,pending=false;uint32_t changed=0,pressed_at=0,released_at=0;
 while(1){uint32_t now=millis();bool sample=gpio_get_level(0)==0;if(sample!=raw){raw=sample;changed=now;}
  if(raw!=stable&&now-changed>=30){stable=raw;if(stable){pressed_at=now;acted=false;}else if(!acted){button_presses++;if(pending&&now-released_at<330){double_press();pending=false;}else{pending=true;released_at=now;}}}
  if(stable&&!acted&&now-pressed_at>=1600){button_holds++;pending=false;hold_press();acted=true;}
  if(pending&&!stable&&now-released_at>=330){single_press();pending=false;}
  vTaskDelay(pdMS_TO_TICKS(10));
 }
}
static void screen_task(void *unused){
 while(1){frame_ms=esp_timer_get_time()/1000;uint32_t now=millis();scout_snapshot(&frame_state);
  if(manual_active(now)||page>=5||frame_state.busy)last_rotation=now;else if(now-last_rotation>=12000){page=(page+1)%PAGE_COUNT;last_rotation=now;}
  apply_geometry();rect(0,0,width,height,BG);header(&frame_state);
  if(page==0)radar(&frame_state);else if(page==1)device_list(&frame_state);else if(page==2)detail(&frame_state);else if(page==3)gatt_view(&frame_state);else if(page==4)evidence(&frame_state);else confirmation(&frame_state);
  footer();if(frame_state.busy&&frame_ms/300%2==0)outline(0,0,width,height,VIOLET);
  while(xSemaphoreTake(flushed,0)==pdTRUE){}if(esp_lcd_panel_draw_bitmap(panel,0,0,width,height,pixels)==ESP_OK&&xSemaphoreTake(flushed,pdMS_TO_TICKS(1000))==pdTRUE)frames++;
  if(capture_requested){capture_requested=false;serial_blob((const uint8_t*)pixels,width*height*2,width,height);xSemaphoreGive(capture_done);}vTaskDelay(pdMS_TO_TICKS(90));
 }
}
void screen_start(int profile,bool invert){
 requested_profile=profile;requested_invert=invert;flushed=xSemaphoreCreateBinary();capture_done=xSemaphoreCreateBinary();pixels=heap_caps_malloc(170*320*2,MALLOC_CAP_DMA);assert(flushed&&capture_done&&pixels);
 spi_bus_config_t bus={.mosi_io_num=23,.miso_io_num=-1,.sclk_io_num=18,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=170*320*2};ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
 esp_lcd_panel_io_handle_t io;esp_lcd_panel_io_spi_config_t cfg={.cs_gpio_num=15,.dc_gpio_num=2,.spi_mode=0,.pclk_hz=16000000,.trans_queue_depth=1,.lcd_cmd_bits=8,.lcd_param_bits=8,.on_color_trans_done=color_done};ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&cfg,&io));
 esp_lcd_panel_dev_config_t dev={.reset_gpio_num=4,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io,&dev,&panel));ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));apply_geometry();ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
 gpio_config_t led={.pin_bit_mask=1ULL<<32,.mode=GPIO_MODE_OUTPUT};ESP_ERROR_CHECK(gpio_config(&led));gpio_set_level(32,1);gpio_config_t button={.pin_bit_mask=1ULL,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};ESP_ERROR_CHECK(gpio_config(&button));
 assert(xTaskCreate(screen_task,"scout_ui",6144,NULL,3,NULL)==pdPASS);assert(xTaskCreate(button_task,"scout_button",3072,NULL,3,NULL)==pdPASS);
}
void screen_page(int value){if(value>=0&&value<PAGE_COUNT){page=value;manual_until=millis()+60000;}}
void screen_profile(int profile,bool invert){requested_profile=profile;requested_invert=invert;}
bool screen_capture(void){while(xSemaphoreTake(capture_done,0)==pdTRUE){}capture_requested=true;return xSemaphoreTake(capture_done,pdMS_TO_TICKS(20000))==pdTRUE;}
unsigned screen_frames(void){return frames;}
int screen_current_page(void){return page;}
int screen_current_profile(void){return applied_profile;}
bool screen_current_invert(void){return applied_invert;}
unsigned screen_button_presses(void){return button_presses;}
unsigned screen_button_holds(void){return button_holds;}
int screen_button_level(void){return gpio_get_level(0);}

#include "adv_parser.h"
#include <string.h>
#include <stdio.h>
bool adv_parse(const uint8_t *data,size_t size,adv_fields_t *out){
 memset(out,0,sizeof(*out));if(size>31)return false;
 for(size_t pos=0;pos<size;){
  unsigned len=data[pos++];if(!len)break;if(len>size-pos)return false;
  uint8_t type=data[pos];const uint8_t *p=&data[pos+1];unsigned bytes=len-1;
  if(type==0x08||type==0x09){unsigned n=bytes<31?bytes:31;for(unsigned i=0;i<n;i++)out->name[i]=p[i]>=32&&p[i]<=126?(char)p[i]:'?';out->name[n]=0;}
  else if(type==0x01){if(bytes!=1)return false;out->flags=p[0];out->has_flags=true;}
  else if(type==0xff){if(bytes<2)return false;out->has_company=true;out->company=p[0]|((uint16_t)p[1]<<8);}
  else if(type==0x02||type==0x03){if(bytes%2)return false;for(unsigned i=0;i<bytes&&out->uuid_count<8;i+=2)out->uuid16[out->uuid_count++]=p[i]|((uint16_t)p[i+1]<<8);}
  else if(type==0x06||type==0x07){
   if(bytes%16)return false;
   if(bytes>=16)snprintf(out->uuid128,sizeof(out->uuid128),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",p[15],p[14],p[13],p[12],p[11],p[10],p[9],p[8],p[7],p[6],p[5],p[4],p[3],p[2],p[1],p[0]);
  }
  pos+=len;
 }
 return true;
}

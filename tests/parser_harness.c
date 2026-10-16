#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "adv_parser.h"
int main(void){
 adv_fields_t out;
 const uint8_t name[]={2,1,6,5,9,'T','E','S','T'};
 assert(adv_parse(name,sizeof(name),&out));assert(!strcmp(out.name,"TEST"));assert(out.has_flags&&out.flags==6);
 const uint8_t company[]={5,255,0x4c,0,1,2};assert(adv_parse(company,sizeof(company),&out));assert(out.has_company&&out.company==0x4c);
 const uint8_t battery[]={3,3,0x0f,0x18};assert(adv_parse(battery,sizeof(battery),&out));assert(out.uuid_count==1&&out.uuid16[0]==0x180f);
 const uint8_t bad[]={5,9,'A'};assert(!adv_parse(bad,sizeof(bad),&out));
 const uint8_t odd[]={2,3,1};assert(!adv_parse(odd,sizeof(odd),&out));
 uint32_t seed=7137;uint8_t data[40];
 for(unsigned run=0;run<20000;run++){
  seed=seed*1664525u+1013904223u;unsigned size=seed%41;
  for(unsigned i=0;i<size;i++){seed=seed*1664525u+1013904223u;data[i]=seed>>24;}
  bool valid=adv_parse(data,size,&out);
  if(valid){assert(out.uuid_count<=8);assert(memchr(out.name,0,sizeof(out.name)));assert(memchr(out.uuid128,0,sizeof(out.uuid128)));}
 }
 return 0;
}

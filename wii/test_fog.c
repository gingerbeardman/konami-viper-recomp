#include "fog.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 uint32_t table[32]={0};
 assert(wii_fog_factor(table,0,0)==1);
 table[0]=0x80402000;
 assert(wii_fog_factor(table,0,8)==33);
 assert(wii_fog_factor(table,1024,8)==129);
 assert(wii_fog_factor(table,2044,8)==145);
 table[31]=0xfffcfffc;
 assert(wii_fog_factor(table,65535,15)==256);
 assert(wii_fog_factor(table,65536,15)==256);
 for(unsigned f=0;f<=256;f++){
  unsigned c=wii_fog_tev_factor(f),actual=c+(c>>7);
  assert(c<=255);assert(f==128?actual==129:actual==f);
 }
 puts("Voodoo mode41 fog table and GX factor mapping tests PASS");
}

#include "../runtime/runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_ram;
static int race_valid(uint32_t address,uint32_t size) {
    return address>=0x38040 && address<RAM_SIZE && size<=RAM_SIZE-address;
}
#include "../runtime/course_reverse.h"
uint32_t rt_mmio_r32(uint32_t a) {(void)a;assert(0);return 0;}
uint32_t rt_mmio_r16(uint32_t a) {(void)a;assert(0);return 0;}
uint32_t rt_mmio_r8(uint32_t a) {(void)a;assert(0);return 0;}
void rt_mmio_w32(uint32_t a,uint32_t v) {(void)a;(void)v;assert(0);}
void rt_mmio_w16(uint32_t a,uint32_t v) {(void)a;(void)v;assert(0);}
void rt_mmio_w8(uint32_t a,uint32_t v) {(void)a;(void)v;assert(0);}
static void section(uint32_t node,uint32_t points,float z,unsigned marker) {
    ST16(node,3);ST32(node+4,points);
    for(unsigned i=0;i<3;i++) {
        STF32(points+32*i,0);STF32(points+32*i+4,z+10*i);
        STF32(points+32*i+8,0);STF32(points+32*i+12,i==2?0:10);
        STF32(points+32*i+16,i==2?0:10);STF32(points+32*i+20,0);
        STF32(points+32*i+24,z+10*i);
    }
    ST8(points+32+28,marker);
}
int main(int argc,char **argv) {
    g_ram=calloc(1,RAM_SIZE);assert(g_ram);
    uint32_t a=0x40000,b=a+0x80,c=b+0x80,root=0;
    section(a,0x41000,0,1);section(b,0x42000,20,3);section(c,0x43000,20,4);
    ST32(a+8,b);ST32(a+12,b);ST32(b+8,a);ST32(b+12,a);
    ST32(a+28,c);ST32(a+32,c);ST32(c+8,a);ST32(c+12,a);
    /* A bad branch must fail without partially modifying the valid main road. */
    ST32(c+52,RAM_SIZE-4);
    uint8_t original[96];memcpy(original,g_ram+0x41000,96);
    assert(!course_reverse_graph(a,40,&root));assert(!memcmp(original,g_ram+0x41000,96));
    ST32(c+52,0);
    assert(course_reverse_graph(a,40,&root) && root==b);
    assert(LDF32(0x41004)==20 && LDF32(0x41000+24)==20);
    assert(LDF32(0x41000+12)==-10 && fabs(fabs(LDF32(0x41000+20))-3.14159265)<.0001f);
    assert(LD8(0x42000+32+28)==4 && LD8(0x43000+32+28)==3);
    assert(LD8(0x41000+32+28)==1 && LD32(a+28)==c && LD32(a+32)==c);
    assert(course_reverse_graph(root,40,&root) && root==a);
    assert(LDF32(0x41004)==0 && LD8(0x42000+32+28)==3);
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f && fread(g_ram,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
        uint32_t native_root=LD32(0x8c0188),next=0;
        uint32_t previous=LD32(native_root+8),course=LD32(0x154da8+0x400);
        assert(race_valid(course,48));float length=LDF32(course+44);
        uint32_t nodes[128]={native_root};unsigned n=1;uint8_t *saved[128]={0};
        for(unsigned i=0;i<n;i++) {
            unsigned count=LD16(nodes[i]);uint32_t data=LD32(nodes[i]+4);
            assert(count>=2 && count<=512 && race_valid(data,count*32));
            saved[i]=malloc(0x44+count*32);assert(saved[i]);
            memcpy(saved[i],g_ram+nodes[i],0x44);memcpy(saved[i]+0x44,g_ram+data,count*32);
            for(unsigned k=0;k<6;k++) {
                uint32_t link=LD32(nodes[i]+8+k*4+(k/2)*12);if(!link) continue;
                unsigned j=0;for(;j<n && nodes[j]!=link;j++);
                if(j==n) {assert(n<128);nodes[n++]=link;}
            }
        }
        assert(course_reverse_graph(native_root,length,&next));
        assert(next==previous);
        assert(course_reverse_graph(next,length,&next) && next==native_root);
        for(unsigned i=0;i<n;i++) {
            assert(!memcmp(saved[i],g_ram+nodes[i],0x44));
            unsigned count=LD16(nodes[i]);uint32_t data=LD32(nodes[i]+4);
            for(unsigned j=0;j<count;j++) {
                uint32_t p=data+j*32;const uint8_t *old=saved[i]+0x44+j*32;
                assert(!memcmp(old,g_ram+p,8)); /* Authored positions remain exact. */
                assert(!memcmp(old+28,g_ram+p+28,4)); /* Checkpoints and flags. */
                uint32_t bits=(uint32_t)old[24]<<24|(uint32_t)old[25]<<16|(uint32_t)old[26]<<8|old[27];
                float distance;memcpy(&distance,&bits,4);
                assert(fabs(distance-LDF32(p+24))<.01f);
            }
            free(saved[i]);
        }
        printf("native graph: %u main/shortcut sections, positions, links, markers and progress verified (%.1f m)\n",n,length);
    }
    free(g_ram);puts("course reverse: branch links, geometry, checkpoint ordering, progress and transactional validation passed");
}

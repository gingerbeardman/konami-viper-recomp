/* Reverse native road graph geometry without changing terrain or collision data.
 * GTI route sections contain 32-byte points and three incoming/outgoing pairs.
 * Validate the complete graph before writing anything. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

static int course_reverse_graph(uint32_t root, float length, uint32_t *new_root) {
    uint32_t nodes[128],data[128];unsigned counts[128],n=1,markers=0;
    if(!isfinite(length) || length<=0 || !race_valid(root,0x44)) return 0;
    nodes[0]=root;
    for(unsigned i=0;i<n;i++) {
        uint32_t node=nodes[i];
        if(!race_valid(node,0x44)) return 0;
        counts[i]=LD16(node);data[i]=LD32(node+4);
        if(counts[i]<2 || counts[i]>512 || !race_valid(data[i],counts[i]*32)) return 0;
        for(unsigned j=0;j<i;j++) {
            /* Aliased point arrays cannot be independently reversed. */
            if(data[i]<data[j]+counts[j]*32 && data[j]<data[i]+counts[i]*32) return 0;
        }
        for(unsigned j=0;j<counts[i];j++) {
            uint32_t p=data[i]+32*j;
            for(unsigned k=0;k<7;k++) if(!isfinite(LDF32(p+4*k))) return 0;
            unsigned marker=LD8(p+28);if(marker>markers) markers=marker;
        }
        for(unsigned k=0;k<6;k++) {
            uint32_t link=LD32(node+8+k*4+(k/2)*12);
            if(!link) continue;
            if(!race_valid(link,0x44)) return 0;
            unsigned j=0;for(;j<n && nodes[j]!=link;j++);
            if(j==n) {if(n==128) return 0;nodes[n++]=link;}
        }
    }
    if(markers<3 || markers>255) return 0;
    uint32_t previous=LD32(root+8);
    if(!previous) return 0;
    for(unsigned i=0;i<n;i++) {
        uint8_t copy[512*32];memcpy(copy,g_ram+data[i],counts[i]*32);
        for(unsigned j=0;j<counts[i];j++) {
            uint32_t p=data[i]+32*j;
            memcpy(g_ram+p,copy+32*(counts[i]-1-j),32);
            STF32(p+24,length-LDF32(p+24));
            unsigned marker=LD8(p+28);
            if(marker>=3) ST8(p+28,markers+3-marker);
        }
        for(unsigned j=0;j+1<counts[i];j++) {
            uint32_t p=data[i]+32*j;
            float dx=LDF32(p+32)-LDF32(p),dz=LDF32(p+36)-LDF32(p+4);
            STF32(p+8,dx);STF32(p+12,dz);STF32(p+16,hypotf(dx,dz));
            STF32(p+20,atan2f(dx,dz));
        }
        uint32_t last=data[i]+32*(counts[i]-1);
        STF32(last+8,0);STF32(last+12,0);STF32(last+16,0);
        STF32(last+20,LDF32(last-32+20));
        for(unsigned k=0;k<3;k++) {
            uint32_t a=nodes[i]+8+k*20,b=a+4,v=LD32(a);
            ST32(a,LD32(b));ST32(b,v);
        }
    }
    if(new_root) *new_root=previous;
    return 1;
}

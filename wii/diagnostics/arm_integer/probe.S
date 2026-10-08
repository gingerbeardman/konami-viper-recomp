.text
.balign 4
.global arithmetic_probe
.type arithmetic_probe,@function
arithmetic_probe:
    mtxer 4
    ori 6,6,1
#if SUBTRACT
    subfme DEST,3
#else
    addme DEST,3
#endif
    mr 3,DEST
    blr
.size arithmetic_probe,.-arithmetic_probe

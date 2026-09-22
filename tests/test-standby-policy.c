#include <assert.h>
#include <stdio.h>
#include "../panel/panel-standby-policy.h"
int main(void) {
 struct sb_inputs s={.deep=1,.stock_sleep=1,.lcd=0,.usb=0,.external=0,.radio=0};
 assert(sb_eligible(&s));assert(sb_block_network(&s));
 s.deep=0;assert(!sb_eligible(&s));assert(sb_block_network(&s));s.deep=1;
 int *guards[]={&s.stock_sleep,&s.lcd,&s.usb,&s.external,&s.radio};
 for(int n=0;n<5;n++) {int before=*guards[n];*guards[n]=-1;assert(!sb_eligible(&s));*guards[n]=before;}
 s.lcd=1;assert(!sb_eligible(&s)&&!sb_block_network(&s));s.lcd=0;
 s.usb=1;assert(!sb_eligible(&s)&&!sb_block_network(&s));s.usb=0;
 s.external=1;assert(!sb_eligible(&s)&&!sb_block_network(&s));s.external=0;
 s.radio=1;assert(!sb_eligible(&s));assert(sb_block_network(&s));s.radio=0;
 s.stock_sleep=0;assert(!sb_eligible(&s)&&!sb_block_network(&s));
 puts("PASS: stock sleep, dark display, disconnected USB/radio, unknown states and wake precedence");
}

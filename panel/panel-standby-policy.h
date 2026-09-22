#ifndef PANEL_STANDBY_POLICY_H
#define PANEL_STANDBY_POLICY_H
/* Unknown values are -1. Stock sleep may deliberately withdraw cellular routes;
 * that is not a fault, even before its radio teardown has finished. */
struct sb_inputs { int deep,stock_sleep,lcd,usb,external,radio; };
static int sb_block_network(const struct sb_inputs *s) {
 return s->stock_sleep==1&&s->lcd==0&&s->usb==0&&s->external==0;
}
static int sb_eligible(const struct sb_inputs *s) {
 return s->deep==1&&sb_block_network(s)&&s->radio==0;
}
#endif

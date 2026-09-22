#ifndef PANEL_TOUCH_GATE_H
#define PANEL_TOUCH_GATE_H
/* Consume dark-screen gestures, including a contact held across power wake. */
struct panel_touch_gate { int blocked; };
static int panel_touch_accept(struct panel_touch_gate *g,int dark,int down,int tap) {
 if(dark){g->blocked=!!down;return 0;}
 if(g->blocked){if(!down)g->blocked=0;return 0;}
 return !!tap;
}
#endif

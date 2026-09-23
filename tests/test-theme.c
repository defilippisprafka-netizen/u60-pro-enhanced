#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "cJSON.h"
static char path[400];
#define PANEL_THEME_FILE path
#include "../panel/panel-theme.h"
int main(void){
 char dir[]="/tmp/u60-theme-test.XXXXXX";assert(mkdtemp(dir));snprintf(path,sizeof(path),"%s/theme",dir);
 assert(panel_theme_load()==0);assert(panel_theme_save("paper"));assert(panel_theme_load()==1);
 assert(!panel_theme_save("amber"));assert(!panel_theme_save("../../bad"));assert(panel_theme_load()==1);
 assert(panel_theme_save("seaglass"));assert(panel_theme_load()==2);
 assert(panel_theme_save("classic"));assert(panel_theme_load()==0);
 FILE*f=fopen(path,"w");fputs("paperjunk",f);fclose(f);assert(panel_theme_load()==0);
 assert(panel_theme_save("paper"));char tmp[512];snprintf(tmp,sizeof(tmp),"%s.next",path);assert(!symlink(path,tmp));assert(!panel_theme_save("classic"));assert(panel_theme_load()==1);unlink(tmp);
 cJSON*i=panel_theme_item(1),*choices=cJSON_GetObjectItem(i,"choices");assert(cJSON_GetArraySize(choices)==3);assert(!strcmp(cJSON_GetObjectItem(i,"value")->valuestring,"纸白蓝"));assert(!strcmp(cJSON_GetObjectItem(i,"action")->valuestring,"screen.theme"));cJSON_Delete(i);
 unlink(path);rmdir(dir);puts("PASS: theme persistence, three choices, rejected unknown/path, corrupt fallback, failed write retains saved theme");
}

#define main control_program_main
#include "../panel/panel-control.c"
#undef main
#include <assert.h>
int main(void){
 char input[100001],out[100010];memset(input,'x',100000);input[100000]=0;
 char *cat[]={"cat",NULL};assert(run_cmd("/bin/cat",cat,input,out,sizeof(out)));assert(!strcmp(input,out));
 char *printf_args[]={"printf","%s","literal;$(this-is-not-a-shell)",NULL};assert(run_cmd("/usr/bin/printf",printf_args,NULL,out,sizeof(out)));assert(!strcmp(out,"literal;$(this-is-not-a-shell)"));
 assert(!run_cmd("/does/not/exist",cat,NULL,out,sizeof(out)));
 char small[4];assert(!run_cmd("/usr/bin/printf",printf_args,NULL,small,sizeof(small)));
 char *sleep_args[]={"sleep","20",NULL};long long start=ms();assert(!run_cmd("/bin/sleep",sleep_args,NULL,out,sizeof(out)));assert(ms()-start<10000);
 puts("PASS process runner stdin, no shell interpolation, missing executable, overflow, timeout");return 0;
}

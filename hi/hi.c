#include <l4/kdebug.h>
#include <sos.h>

int main(void) {
  char *input = (char *) 0x2000000;
  int size = 8 * 4096;
  for(int i=0;i<size;i++) {
    input[i] = 'Y';
  }
  /*  while(1) {
    ;
    }*/
  return 0;
}

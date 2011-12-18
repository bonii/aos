#include <l4/kdebug.h>
#include <sos.h>
#include <stdio.h>

int main(void) {
  char *input1 = (char *) 0x2000000;
  int size = 4 * 4096;
  for(int i=0;i<size;i++) {
    input1[i] = 'X';
  }
  while(1) {
    ;
  }
  return 0;
}

#include <l4/kdebug.h>
#include <sos.h>

int main(void) {
  L4_KDB_PrintChar('E');
  while(1) {
    ;
  }
  return 0;
}

#include <stdlib.h>
#include <stdio.h>

#include "luv_hash.h"
#include "luv_list.h"

int main(int argc, char* argv[]) {
  luv__hash_self_test();
  luv__list_self_test();
  printf("Tests passed OK\n");
  return 0;
}



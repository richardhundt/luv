#include <stdlib.h>
#include <stdio.h>

#include "ray_hash.h"
#include "ray_list.h"

int main(int argc, char* argv[]) {
  ray__hash_self_test();
  ray__list_self_test();
  printf("Tests passed OK\n");
  return 0;
}



#include <cassert>

#include "arxt.hpp"


int
main()
{
  arxt::radix256_node node;
  arxt::insert(&node, "asdfasdf");
  arxt::insert(&node, "asdfqwer");

  assert(arxt::find(&node, "asdfasdf") != nullptr);
  assert(arxt::find(&node, "asdfqwer") != nullptr);
  assert(arxt::find(&node, "asdf") != nullptr);
  assert(arxt::find(&node, "as") == nullptr);
}
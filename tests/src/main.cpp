#include <gtest/gtest.h>

int main(int argc, char **argv) {
  printf("Hello, world!\n");

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
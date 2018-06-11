#include "gtest/gtest.h"

#include <vector>

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(example, sum_zero) {
  auto result = 0;
  ASSERT_EQ(result, 0);
}

TEST(example, sum_five) {
  auto result = 15;
  ASSERT_EQ(result, 15);
}

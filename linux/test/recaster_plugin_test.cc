#include <flutter_linux/flutter_linux.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "include/recaster/recaster_plugin.h"
#include "recaster_plugin_private.h"

namespace recaster {
namespace test {

TEST(RecasterPlugin, GetPlatformVersion) {
  g_autoptr(FlMethodResponse) response = get_platform_version();
  ASSERT_NE(response, nullptr);
  ASSERT_TRUE(FL_IS_METHOD_SUCCESS_RESPONSE(response));
  FlValue* result = fl_method_success_response_get_result(
      FL_METHOD_SUCCESS_RESPONSE(response));
  ASSERT_EQ(fl_value_get_type(result), FL_VALUE_TYPE_STRING);
  EXPECT_THAT(fl_value_get_string(result), testing::StartsWith("Linux "));
}

}
}

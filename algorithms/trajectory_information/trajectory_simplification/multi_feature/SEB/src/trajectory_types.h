#pragma once

#include <cstddef>
#include <cstdint>

struct RawPoint {
  std::int64_t time = 0;
  double lon = 0.0;
  double lat = 0.0;
  std::size_t original_index = 0;
};

struct MetricPoint : RawPoint {
  double x = 0.0;
  double y = 0.0;
};

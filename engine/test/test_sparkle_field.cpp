#include "doctest.h"
#include "SparkleField.h"
#include <array>
#include <cmath>

TEST_CASE("Sparkle Field synthesizes finite safe directional 5.1 with silent LFE")
{
  const auto textures = {OhlSparkleField::Texture::Mixed,
                         OhlSparkleField::Texture::Sine,
                         OhlSparkleField::Texture::Triangle,
                         OhlSparkleField::Texture::Metal,
                         OhlSparkleField::Texture::Noise};
  for (const auto texture : textures)
  {
    OhlSparkleField field;
    field.Configure(48000, 9, 240, texture);
    bool front = false, rear = false;
    float peak = 0.0f;
    for (int i = 0; i < 48000 * 2; ++i)
    {
      std::array<float, 6> frame{};
      field.Render(frame.data(), 0.0316227766f);
      CHECK(frame[3] == 0.0f);
      for (float sample : frame)
      {
        CHECK(std::isfinite(sample));
        peak = std::max(peak, std::abs(sample));
      }
      front = front || frame[0] != 0.0f || frame[1] != 0.0f || frame[2] != 0.0f;
      rear = rear || frame[4] != 0.0f || frame[5] != 0.0f;
    }
    CHECK(front);
    CHECK(rear);
    CHECK(peak < 0.1f);
  }
}

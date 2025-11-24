#include "Image.h"
#include "MathUtilities.h"

namespace
{
  [[nodiscard]] auto NormalizeKernel(auto kernel)
  {
    float sumWeight = 0;
    for (const auto& [offset, weight] : kernel)
    {
      sumWeight += weight;
    }

    DEBUG_ASSERT(sumWeight > 0);

    for (auto& [offset, weight] : kernel)
    {
      weight /= sumWeight;
    }

    return kernel;
  }

  template<typename OffsetType>
  std::vector<std::pair<OffsetType, float>> CreateSeparableGaussianKernel(Core::DSP::SeparableKernelDirection direction, int width, float stddev, bool normalized)
  {
    ASSERT(width > 0);
    auto kernel = std::vector<std::pair<OffsetType, float>>();

    for (int i = -width / 2; i < (width + 1) / 2; i++)
    {
      auto offset = OffsetType(0);
      if constexpr (!std::is_same_v<OffsetType, int>)
      {
        offset[static_cast<int>(direction)] = i;
      }
      kernel.emplace_back(offset, Math::GaussianNorm(float(i), 0, stddev));
    }

    if (normalized)
    {
      kernel = NormalizeKernel(std::move(kernel));
    }

    return kernel;
  }

  template<typename OffsetType>
  std::vector<std::pair<OffsetType, float>> CreateSeparableBoxKernel(Core::DSP::SeparableKernelDirection direction, int width)
  {
    ASSERT(width > 0);
    auto kernel = std::vector<std::pair<OffsetType, float>>();

    for (int i = -width / 2; i < (width + 1) / 2; i++)
    {
      auto offset = OffsetType(0);
      if constexpr (!std::is_same_v<OffsetType, int>)
      {
        offset[static_cast<int>(direction)] = i;
      }
      kernel.emplace_back(offset, float(i) / width);
    }

    // Box kernel is always normalized.
    return kernel;
  }
} // namespace

std::vector<std::pair<int, float>> Core::DSP::CreateGaussianKernel1D(int width, float stddev, bool normalized)
{
  return CreateSeparableGaussianKernel<int>(SeparableKernelDirection::X, width, stddev, normalized);
}

std::vector<std::pair<glm::ivec2, float>> Core::DSP::CreateSeparableGaussianKernel2D(SeparableKernelDirection direction, int width, float stddev, bool normalized)
{
  return CreateSeparableGaussianKernel<glm::ivec2>(direction, width, stddev, normalized);
}

std::vector<std::pair<glm::ivec3, float>> Core::DSP::CreateSeparableGaussianKernel3D(SeparableKernelDirection direction, int width, float stddev, bool normalized)
{
  return CreateSeparableGaussianKernel<glm::ivec3>(direction, width, stddev, normalized);
}

std::vector<std::pair<int, float>> Core::DSP::CreateBoxKernel1D(int width)
{
  return CreateSeparableBoxKernel<int>(SeparableKernelDirection::X, width);
}

std::vector<std::pair<glm::ivec2, float>> Core::DSP::CreateSeparableBoxKernel2D(SeparableKernelDirection direction, int width)
{
  return CreateSeparableBoxKernel<glm::ivec2>(direction, width);
}

std::vector<std::pair<glm::ivec3, float>> Core::DSP::CreateSeparableBoxKernel3D(SeparableKernelDirection direction, int width)
{
  return CreateSeparableBoxKernel<glm::ivec3>(direction, width);
}

#include "doctest.h"

TEST_CASE("DSP::Image")
{
  auto image = Core::DSP::Image<2, float>({3, 3});
  REQUIRE(image.Size() == glm::ivec2{3, 3});

  SUBCASE("Fill")
  {
    constexpr float value = 1.25f;
    image.Fill(value);
    for (int y = 0; y < image.Size().y; y++)
    {
      for (int x = 0; x < image.Size().x; x++)
      {
        CHECK(image.Load({x, y}) == value);
      }
    }
  }

  SUBCASE("Store and load")
  {
    image.Fill(0);
    float value = 1;
    for (int y = 0; y < image.Size().y; y++)
    {
      for (int x = 0; x < image.Size().x; x++)
      {
        image.Store({x, y}, value);
        CHECK(image.Load({x, y}) == value);
        value++;
      }
    }
  }

  SUBCASE("Sampling")
  {
    // 7 8 9
    // 4 5 6
    // 1 2 3
    float value = 1;
    for (int y = 0; y < image.Size().y; y++)
    {
      for (int x = 0; x < image.Size().x; x++)
      {
        image.Store({x, y}, value);
        value++;
      }
    }

    SUBCASE("Filter::Nearest")
    {
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Nearest}, {0.9f, 0.1f}), 3);
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Nearest}, {0.5f, 0.5f}), 5);
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Nearest}, {0.1f, 0.9f}), 7);
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Nearest}, {2.0f / 3 + 1e-4f, 2.0f / 3 + 1e-4f}), 9);
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Nearest}, {2.0f / 3 - 1e-4f, 2.0f / 3 - 1e-4f}), 5);
    }

    SUBCASE("Filter::Linear")
    {
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Linear}, {0.5f, 0.5f}), doctest::Approx(5));
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Linear}, {0.5f + 1e-4f, 0.5f + 1e-4f}), doctest::Approx(5).epsilon(0.01));
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Linear}, {0.5f - 1e-4f, 0.5f - 1e-4f}), doctest::Approx(5).epsilon(0.01));
      CHECK_EQ(image.Sample({.filter = Core::DSP::Filter::Linear}, {1.0f / 3, 1.0f / 3}), doctest::Approx(3));
    }
  }

  SUBCASE("Convolution")
  {
    auto image2 = Core::DSP::Image<2, float>({11, 11});
    image2.Fill(0);
    // Kernel should be repeated in the image at the rows with these points.
    image2.Store({5, 0}, 1);
    image2.Store({5, 5}, 1);
    image2.Store({5, 10}, 1);
    const auto kernel = Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::X, 11, 3);
    REQUIRE(kernel.size() == image2.Size().x);

    const auto convolved = image2.Convolve(kernel, Core::DSP::WrapMode::Skip, Core::DSP::WeightNormMode::DoNotNormalize);
    for (size_t i = 0; i < kernel.size(); i++)
    {
      CHECK_EQ(kernel[i].second, doctest::Approx(convolved.Load({i, 0})));
      CHECK_EQ(kernel[i].second, doctest::Approx(convolved.Load({i, 5})));
      CHECK_EQ(kernel[i].second, doctest::Approx(convolved.Load({i, 10})));
    }
  }
}

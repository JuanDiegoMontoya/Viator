#pragma once
#include "Core/Assert2.h"

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <memory>
#include <type_traits>

namespace Core::DSP
{
  namespace ImageHelper
  {
    template<typename T>
    concept Filterable = requires(T x)
    {
      { x + x * x } -> std::same_as<T>;
    };

    template<typename T>
    concept Iterable = requires(T x)
    {
      { std::begin(x) };
      { std::end(x) };
    };

    struct BindsToAll
    {
      template<typename T>
      operator T()
      {
        return T{};
      }
    };

    template<typename Fn>
    concept HasTwoArguments = requires(Fn f)
    {
      { f(BindsToAll{}, BindsToAll{}) };
    };
  } // namespace ImageHelper

  enum class Filter
  {
    Nearest,
    Linear,
  };

  enum class WrapMode
  {
    Clamp,
    Repeat,
    MirrorRepeat,
  };

  struct Sampler
  {
    Filter filter = Filter::Nearest;
    WrapMode wrapMode = WrapMode::Clamp;
  };

  template<size_t Dim, typename T>
    requires(Dim >= 1 && Dim <= 3)
  class Image
  {
  public:
    using value_type      = T;
    using dimensions_type = std::conditional_t<Dim == 1, int, std::conditional_t<Dim == 2, glm::ivec2, glm::ivec3>>;
    using uv_type         = std::conditional_t<Dim == 1, float, std::conditional_t<Dim == 2, glm::vec2, glm::vec3>>;

    constexpr Image() = default;

    explicit constexpr Image(dimensions_type imageSize) : imageSize_(imageSize)
    {
      image_ = std::make_unique<T[]>(NumTexels());
    }

    void constexpr Fill(T value)
    {
      std::fill_n(data(), NumTexels(), value);
    }

    [[nodiscard]] constexpr T Load(dimensions_type p) const noexcept
    {
      DEBUG_ASSERT(glm::all(glm::greaterThanEqual(p, dimensions_type(0))) && glm::all(glm::lessThan(p, dimensions_type(imageSize_))));
      return image_[TexCoordToIndex(p)];
    }

    [[nodiscard]] constexpr T LoadWrapped(dimensions_type p, WrapMode wrapMode) const noexcept
    {
      return image_[TexCoordToIndex(GetWrappedCoord(p, wrapMode))];
    }

    constexpr void Store(dimensions_type p, T value) noexcept
    {
      DEBUG_ASSERT(glm::all(glm::greaterThanEqual(p, dimensions_type(0))) && glm::all(glm::lessThan(p, dimensions_type(imageSize_))));
      image_[TexCoordToIndex(p)] = value;
    }

    [[nodiscard]] constexpr T Sample(Sampler sampler, uv_type uv) const noexcept
      requires ImageHelper::Filterable<T>
    {
      const auto unnormalized = uv * uv_type(imageSize_);

      if (sampler.filter == Filter::Nearest)
      {
        return Load(dimensions_type(unnormalized));
      }

      const auto intCoord = dimensions_type(unnormalized);

      if constexpr (Dim == 1)
      {
        const auto l = Load(intCoord + dimensions_type(0));
        const auto r = Load(intCoord + dimensions_type(1));

        const auto weight = unnormalized - uv_type(intCoord);
        return glm::mix(l, r, weight);
      }

      if constexpr (Dim == 2)
      {
        const auto bl = Load(intCoord + dimensions_type(0, 0));
        const auto br = Load(intCoord + dimensions_type(1, 0));
        const auto tl = Load(intCoord + dimensions_type(0, 1));
        const auto tr = Load(intCoord + dimensions_type(1, 1));

        const auto weight = unnormalized - uv_type(intCoord);
        return glm::mix(glm::mix(bl, br, weight.x), glm::mix(tl, tr, weight.x), weight.y);
      }

      if constexpr (Dim == 3)
      {
        const auto bln = Load(intCoord + dimensions_type(0, 0, 0));
        const auto brn = Load(intCoord + dimensions_type(1, 0, 0));
        const auto tln = Load(intCoord + dimensions_type(0, 1, 0));
        const auto trn = Load(intCoord + dimensions_type(1, 1, 0));
        const auto blf = Load(intCoord + dimensions_type(0, 0, 1));
        const auto brf = Load(intCoord + dimensions_type(1, 0, 1));
        const auto tlf = Load(intCoord + dimensions_type(0, 1, 1));
        const auto trf = Load(intCoord + dimensions_type(1, 1, 1));

        const auto weight = unnormalized - uv_type(intCoord);
        const auto n      = glm::mix(glm::mix(bln, brn, weight.x), glm::mix(tln, trn, weight.x), weight.y);
        const auto f      = glm::mix(glm::mix(blf, brf, weight.x), glm::mix(tlf, trf, weight.x), weight.y);
        return glm::mix(n, f, weight.z);
      }
    }

    constexpr T* data() noexcept
    {
      return image_.get();
    }

    constexpr const T* data() const noexcept
    {
      return image_.get();
    }

    constexpr dimensions_type ImageSize() const noexcept
    {
      return imageSize_;
    }

    static constexpr size_t Rank() noexcept
    {
      return Dim;
    }

    // Special operations
    [[nodiscard]] constexpr Image Convolve(const ImageHelper::Iterable auto& kernel1D) const
      requires ImageHelper::Filterable<T>
    {
      auto out = Image(ImageSize());

      if constexpr (Dim == 2)
      {
        for (int y = 0; y < ImageSize().y; y++)
        {
          for (int x = 0; x < ImageSize().x; x++)
          {
            auto sumValue        = T{};
            auto sumWeight       = T{};
            const auto centerPos = dimensions_type{x, y};

            for (const auto& [offset, weight] : kernel1D)
            {
              const auto samplePos = centerPos + offset;
              if (glm::any(glm::lessThan(samplePos, dimensions_type(0))) || glm::any(glm::greaterThanEqual(samplePos, ImageSize())))
              {
                continue;
              }

              sumValue += weight * Load(samplePos);
              sumWeight += weight;
            }

            out.Store(centerPos, sumValue / sumWeight);
          }
        }
      }
      else
      {
        static_assert(false, "Convolve is only implemented for 2D images.");
      }

      return out;
    }

    template<typename T2 = T>
    [[nodiscard]] constexpr auto Map(auto&& fn) const
    {
      auto out = Image<Dim, T2>(ImageSize());

      if constexpr (Dim == 2)
      {
        for (int y = 0; y < ImageSize().y; y++)
        {
          for (int x = 0; x < ImageSize().x; x++)
          {
            if constexpr (ImageHelper::HasTwoArguments<std::remove_cvref_t<decltype(fn)>>)
            {
              out.Store({x, y}, static_cast<T>(fn(glm::ivec2{x, y}, Load({x, y}))));
            }
            else
            {
              out.Store({x, y}, static_cast<T>(fn(Load({x, y}))));
            }
          }
        }
      }
      else
      {
        static_assert(false, "Map is only implemented for 2D images.");
      }

      return out;
    }

  private:
    [[nodiscard]] constexpr int TexCoordToIndex(dimensions_type p) const noexcept
    {
      if constexpr (Dim == 1)
      {
        return p;
      }

      if constexpr (Dim == 2)
      {
        return p.x + p.y * imageSize_.x;
      }

      if constexpr (Dim == 3)
      {
        return p.x + p.y * imageSize_.x + p.z * imageSize_.y * imageSize_.x;
      }
    }

    [[nodiscard]] constexpr int NumTexels() const noexcept
    {
      if constexpr (Dim == 1)
      {
        return imageSize_;
      }

      if constexpr (Dim == 2)
      {
        return imageSize_.x * imageSize_.y;
      }

      if constexpr (Dim == 3)
      {
        return imageSize_.x * imageSize_.y * imageSize_.z;
      }
    }

    [[nodiscard]] static constexpr dimensions_type Mirror(dimensions_type p) noexcept
    {
      // glspec46.core.pdf, 8.14.2: Coordinate Wrapping and Texel Selection
      const auto lessThanZero = glm::lessThan(p, dimensions_type(0));
      const auto alt          = -(1 + p);
      return glm::mix(p, alt, lessThanZero);
    }

    [[nodiscard]] constexpr dimensions_type GetWrappedCoord(dimensions_type p, WrapMode wrapMode) const
    {
      if (wrapMode == WrapMode::Clamp)
      {
        return glm::clamp(p, dimensions_type(0), dimensions_type(imageSize_ - 1));
      }

      if (wrapMode == WrapMode::Repeat)
      {
        return p % imageSize_;
      }

      if (wrapMode == WrapMode::MirrorRepeat)
      {
        return (imageSize_ - 1) - Mirror(p % (2 * imageSize_)) - imageSize_;
      }

      PANIC;
    }

    dimensions_type imageSize_{};
    std::unique_ptr<T[]> image_;
  };
} // namespace Core

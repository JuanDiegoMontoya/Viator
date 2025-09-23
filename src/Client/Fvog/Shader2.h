#pragma once
#include "detail/VkFwd.h"
#include "BasicTypes2.h"

#include <string>
#include <string_view>
#include <filesystem>

namespace Fvog
{
  namespace detail
  {
    struct ShaderCompileInfo;
  }

  enum class PipelineStage
  {
    VERTEX_SHADER,
    FRAGMENT_SHADER,
    COMPUTE_SHADER,
    RAYGEN_SHADER,
    MISS_SHADER,
    CLOSEST_HIT_SHADER,
    ANY_HIT_SHADER,
    INTERSECTION_SHADER,
  };

  /// @brief A shader object to be used in one or more GraphicsPipeline or ComputePipeline objects
  class Shader
  {
  public:
    /// @brief Constructs the shader
    /// @param stage A pipeline stage
    /// @param source A GLSL source string
    /// @throws ShaderCompilationException if the shader is malformed
    // Already-processed source constructor
    explicit Shader(PipelineStage stage, std::string_view source, std::string name = {});

    // Path constructor (uses glslang include handling)
    explicit Shader(PipelineStage stage, const std::filesystem::path& path, std::string name = {});
    Shader(const Shader&) = delete;
    Shader(Shader&& old) noexcept;
    Shader& operator=(const Shader&) = delete;
    Shader& operator=(Shader&& old) noexcept;
    ~Shader();

    /// @brief Gets the handle of the underlying OpenGL shader object
    /// @return The shader
    [[nodiscard]] VkShaderModule Handle() const;
    [[nodiscard]] Extent3D WorkgroupSize() const;
    [[nodiscard]] PipelineStage GetPipelineStage() const;

  private:
    void Initialize(const detail::ShaderCompileInfo& info);

    std::string name_;
    PipelineStage stage_{};
    VkShaderModule shaderModule_;
    Extent3D workgroupSize_{};
  };
}

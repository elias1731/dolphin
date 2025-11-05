// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef WINRT_XBOX

#include "VideoCommon/XboxUberShaderWorkarounds.h"
#include "VideoCommon/UberShaderVertex.h"
#include "VideoCommon/UberShaderPixel.h"
#include "VideoCommon/UberShaderCommon.h"
#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/VideoConfig.h"

namespace VideoCommon
{
namespace
{

struct ShaderFilteringLimits
{
  u32 max_texgens;                          // Maximum texture generators allowed
  u32 max_texgens_with_dual_src;            // Maximum texgens when dual-source blending is used
  u32 max_texgens_with_early_depth;         // Maximum texgens when early depth is enabled
};

// Get filtering limits based on the current shader compilation mode
constexpr ShaderFilteringLimits GetFilteringLimits(ShaderCompilationMode mode)
{
  switch (mode)
  {
  case ShaderCompilationMode::SynchronousUberShaders:
  {
    // Exclusive mode is the most permissive as only ubershaders are used
    ShaderFilteringLimits limits{};
    limits.max_texgens = 8;
    limits.max_texgens_with_dual_src = 6;
    limits.max_texgens_with_early_depth = 6;
    return limits;
  }
  
  case ShaderCompilationMode::AsynchronousUberShaders:
  {
    // Hybrid mode is more restrictive since both ubershaders AND specialized shaders compile
    ShaderFilteringLimits limits{};
    limits.max_texgens = 6;
    limits.max_texgens_with_dual_src = 4;
    limits.max_texgens_with_early_depth = 4;
    return limits;
  }
  
  default:
  {
    // Default mode is the most restrictive for stability
    ShaderFilteringLimits limits{};
    limits.max_texgens = 4;
    limits.max_texgens_with_dual_src = 4;
    limits.max_texgens_with_early_depth = 4;
    return limits;
  }
  }
}

}  // namespace

bool XboxPrecompiledShaders::IsUsingExclusiveUberShaders()
{
  return g_ActiveConfig.iShaderCompilationMode == ShaderCompilationMode::SynchronousUberShaders;
}

bool XboxPrecompiledShaders::IsUsingHybridUberShaders()
{
  return g_ActiveConfig.iShaderCompilationMode == ShaderCompilationMode::AsynchronousUberShaders;
}

bool XboxPrecompiledShaders::ShouldUseConservativeFiltering()
{
  // Use conservative filtering in any ubershader mode to avoid driver issues
  return IsUsingHybridUberShaders() || IsUsingExclusiveUberShaders();
}

ShaderCompilationMode XboxPrecompiledShaders::GetRecommendedXboxMode()
{
  return ShaderCompilationMode::SynchronousUberShaders;
}

bool XboxPrecompiledShaders::ShouldPrecompileShader(const UberShader::VertexShaderUid& vs_uid,
                                                    const GeometryShaderUid& gs_uid,
                                                    const UberShader::PixelShaderUid& ps_uid)
{
  
  // Require passthrough geometry shaders to reduce complexity
  if (!gs_uid.GetUidData()->IsPassthrough())
    return false;
  
  const UberShader::vertex_ubershader_uid_data* vs_data = vs_uid.GetUidData();
  const UberShader::pixel_ubershader_uid_data* ps_data = ps_uid.GetUidData();
  
  // Vertex and pixel shaders must have matching texgen counts
  if (vs_data->num_texgens != ps_data->num_texgens)
    return false;
  
  // Get filtering limits based on current compilation mode
  const ShaderFilteringLimits limits =
      GetFilteringLimits(g_ActiveConfig.iShaderCompilationMode);
  
  const u32 num_texgens = ps_data->num_texgens;
  
  // Check base texgen limit
  if (num_texgens > limits.max_texgens)
    return false;
  
  // Apply stricter limits for complex features
  if (!ps_data->no_dual_src && num_texgens > limits.max_texgens_with_dual_src)
    return false;
  
  if (ps_data->early_depth && num_texgens > limits.max_texgens_with_early_depth)
    return false;
  
  return true;
}

void XboxPrecompiledShaders::ApplyXboxOptimizations()
{
  // TODO: Implement Xbox-specific optimizations through VideoConfig settings later on
}

void XboxPrecompiledShaders::InitializeEssentialShaderList()
{
  // TODO: Pre-populate a list of essential shader combinations for faster startup later on
}

}  // namespace VideoCommon

#endif  // WINRT_XBOX
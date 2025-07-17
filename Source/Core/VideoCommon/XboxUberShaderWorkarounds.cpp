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

// Xbox ubershader mode utility functions - defined first so they can be used by other functions
bool XboxPrecompiledShaders::IsUsingExclusiveUberShaders()
{
  // Check if we're in exclusive ubershader mode (SynchronousUberShaders)
  // In this mode, only ubershaders are used, never specialized shaders
  return g_ActiveConfig.iShaderCompilationMode == ShaderCompilationMode::SynchronousUberShaders;
}

bool XboxPrecompiledShaders::IsUsingHybridUberShaders()
{
  // Check if we're in hybrid ubershader mode (AsynchronousUberShaders)
  // In this mode, ubershaders are used initially, then specialized shaders when ready
  return g_ActiveConfig.iShaderCompilationMode == ShaderCompilationMode::AsynchronousUberShaders;
}

bool XboxPrecompiledShaders::ShouldUseConservativeFiltering()
{
  // Use more conservative shader filtering on Xbox, especially in hybrid mode
  // Hybrid mode needs to be more restrictive since both ubershaders AND specialized shaders compile
  return IsUsingHybridUberShaders() || IsUsingExclusiveUberShaders();
}

ShaderCompilationMode XboxPrecompiledShaders::GetRecommendedXboxMode()
{
  // For Xbox, I strongly recommend Exclusive UberShaders (SynchronousUberShaders)
  // This is the most stable mode for Xbox graphics drivers from my testing.
  // and avoids the potential stuttering issues from shader transitions
  return ShaderCompilationMode::SynchronousUberShaders;
}

bool XboxPrecompiledShaders::ShouldPrecompileShader(const UberShader::VertexShaderUid& vs_uid,
                                                    const GeometryShaderUid& gs_uid,
                                                    const UberShader::PixelShaderUid& ps_uid)
{
  // On Xbox, only precompile essential shader combinations to avoid driver issues
  
  // Only use passthrough geometry shaders to reduce complexity
  if (!gs_uid.GetUidData()->IsPassthrough())
    return false;
  
  // Get data for analysis
  const UberShader::vertex_ubershader_uid_data* vs_data = vs_uid.GetUidData();
  const UberShader::pixel_ubershader_uid_data* ps_data = ps_uid.GetUidData();
  
  // Must have matching texgen counts
  if (vs_data->num_texgens != ps_data->num_texgens)
    return false;
  
  // Apply different filtering based on ubershader mode
  if (IsUsingExclusiveUberShaders())
  {
    // EXCLUSIVE MODE: Only ubershaders are used, so we can be slightly more permissive
    // since we don't need to worry about specialized shader compilation load
    
    // Limit to moderate texture complexity for Xbox driver stability
    if (ps_data->num_texgens > 6) // Allow up to 6 texgens in exclusive mode
      return false;
    
    // Skip dual source blending with high complexity
    if (!ps_data->no_dual_src && ps_data->num_texgens > 3)
      return false;
  }
  else if (IsUsingHybridUberShaders())
  {
    // HYBRID MODE: Both ubershaders AND specialized shaders compile
    // Be more conservative to reduce total compilation load on Xbox
    
    // Stricter limit for hybrid mode due to dual compilation load
    if (ps_data->num_texgens > 4) // More restrictive in hybrid mode
      return false;
    
    // Be more aggressive about avoiding dual source blending in hybrid mode
    if (!ps_data->no_dual_src && ps_data->num_texgens > 2)
      return false;
    
    // Skip early depth with any significant complexity in hybrid mode
    if (ps_data->early_depth && ps_data->num_texgens > 2)
      return false;
  }
  else
  {
    // Other modes (synchronous, skip rendering) - minimal ubershader precompilation
    // Only compile the most basic shader combinations
    if (ps_data->num_texgens > 2)
      return false;
  }
  
  // Common restrictions for all modes
  // Skip early depth configurations with high texture complexity
  if (ps_data->early_depth && ps_data->num_texgens > 3)
    return false;
  
  return true;
}

void XboxPrecompiledShaders::ApplyXboxOptimizations()
{
  // Apply Xbox-specific optimizations to improve ubershader compatibility
  
  // Force conservative settings that work better with Xbox graphics driver
  // Note: Probably should do this through the VideoConfig settings one day
  
  // Xbox-specific mode recommendations:
  if (IsUsingExclusiveUberShaders())
  {
  }
  else if (IsUsingHybridUberShaders())
  {
  }
  else
  {
  }
}

void XboxPrecompiledShaders::InitializeEssentialShaderList()
{
}

} // namespace VideoCommon

#endif // WINRT_XBOX
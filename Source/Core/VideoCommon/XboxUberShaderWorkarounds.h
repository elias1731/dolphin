// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef WINRT_XBOX

#include "Common/CommonTypes.h"
#include "VideoCommon/UberShaderVertex.h"
#include "VideoCommon/UberShaderPixel.h"
#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/VideoConfig.h"

// Xbox-specific ubershader workarounds for graphics driver compatibility

namespace VideoCommon
{

// Xbox-specific shader configurations that are known to work well with the Xbox graphics driver
// These configurations are precompiled to avoid runtime compilation issues

struct XboxUberShaderConfig
{
  // Common ubershader configurations that work reliably on Xbox
  static constexpr bool USE_SIMPLIFIED_BLENDING = true;
  static constexpr bool PREFER_PASSTHROUGH_GEOMETRY = true;
  static constexpr bool LIMIT_TEXTURE_VARIANTS = true;
  static constexpr bool USE_CONSERVATIVE_COMPILATION = true;
  
  // Thread limits for Xbox compilation
  static constexpr u32 MAX_COMPILER_THREADS = 2;
  static constexpr u32 MAX_SHADER_VARIANTS = 64; // Reduced from default
};

// Precompiled shader combinations known to work on Xbox
// This list should be expanded based on testing and game compatibility
class XboxPrecompiledShaders
{
public:
  // Check if a shader combination should be precompiled for Xbox compatibility
  static bool ShouldPrecompileShader(const UberShader::VertexShaderUid& vs_uid,
                                     const GeometryShaderUid& gs_uid,
                                     const UberShader::PixelShaderUid& ps_uid);
  
  // Get Xbox-optimized shader compilation settings
  static void ApplyXboxOptimizations();
  
  // Xbox ubershader mode utilities
  static bool IsUsingExclusiveUberShaders(); // SynchronousUberShaders mode
  static bool IsUsingHybridUberShaders();    // AsynchronousUberShaders mode
  static bool ShouldUseConservativeFiltering(); // More restrictive shader selection for Xbox
  
  // Get recommended ubershader mode for Xbox
  static ShaderCompilationMode GetRecommendedXboxMode();
  
private:
  // List of essential shader combinations for Xbox
  static void InitializeEssentialShaderList();
};

} // namespace VideoCommon

#endif // WINRT_XBOX

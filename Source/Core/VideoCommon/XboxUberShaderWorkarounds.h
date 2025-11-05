// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef WINRT_XBOX

#include "Common/CommonTypes.h"
#include "VideoCommon/UberShaderVertex.h"
#include "VideoCommon/UberShaderPixel.h"
#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/VideoConfig.h"

namespace VideoCommon
{

struct XboxUberShaderConfig
{
  static constexpr bool USE_SIMPLIFIED_BLENDING = true;
  static constexpr bool PREFER_PASSTHROUGH_GEOMETRY = true;
  static constexpr bool LIMIT_TEXTURE_VARIANTS = true;
  static constexpr bool USE_CONSERVATIVE_COMPILATION = true;
  
  static constexpr u32 MAX_COMPILER_THREADS = 2;
  static constexpr u32 MAX_SHADER_VARIANTS = 64;
};

class XboxPrecompiledShaders
{
public:
  // Shader compilation mode queries
  static bool IsUsingExclusiveUberShaders();   // SynchronousUberShaders mode
  static bool IsUsingHybridUberShaders();       // AsynchronousUberShaders mode
  static bool ShouldUseConservativeFiltering();
  static ShaderCompilationMode GetRecommendedXboxMode();
  
  // Shader precompilation decisions
  static bool ShouldPrecompileShader(const UberShader::VertexShaderUid& vs_uid,
                                     const GeometryShaderUid& gs_uid,
                                     const UberShader::PixelShaderUid& ps_uid);
  
  // Optimization and initialization
  static void ApplyXboxOptimizations();

private:
  static void InitializeEssentialShaderList();
};

}  // namespace VideoCommon

#endif  // WINRT_XBOX

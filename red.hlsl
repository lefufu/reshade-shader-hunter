// constant red 

// shader injection in cb13
#include "H:\utils\Repos\ShaderHunter\mod_parameters.h"

cbuffer cb13 : register(b13)
{
    ShaderInjectData injectedData : packoffset(c0);
}


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  o0.xyzw = float4(0.1, 0.1, 0.1, 1);
    
    if (injectedData.colorFlag == 0)  o0.xyzw = float4(1, 1, 1, 1);
    if (injectedData.colorFlag == 1)  o0.xyzw = float4(1, 0, 0, 1);
    if (injectedData.colorFlag == 2)  o0.xyzw = float4(0, 1, 0, 1);
    if (injectedData.colorFlag == 3)  o0.xyzw = float4(1, 0, 1, 1);
    if (injectedData.colorFlag == 4)  o0.xyzw = float4(1, 0, 1, 1);

  return;
}

// size of the constant buffer containing all mod parameters, to be injected in shaders
const int CBSIZE = 4;

// CB number to be injected in the shaders
const int CBINDEX = 13;


// Must be 32bit aligned
// Should be 4x32
struct ShaderInjectData {
	float colorFlag;
	float unused1;
	float unused2;
	float unused3;
};

/* 
#ifndef __cplusplus
cbuffer cb13 : register(b13) {
	ShaderInjectData injectedData : packoffset(c0);
}
#endif */

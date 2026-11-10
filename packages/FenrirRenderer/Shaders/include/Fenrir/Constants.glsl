// Fenrir/Constants.glsl: the numbers every built-in shader shares, so they agree to the last bit. It binds nothing.
#ifndef FENRIR_CONSTANTS_GLSL
#define FENRIR_CONSTANTS_GLSL

const float FenrirPi = 3.14159265;
// Rec. 709 luminance's weights: dot a linear colour with them for its luminance.
const vec3 FenrirLuminance = vec3(0.2126, 0.7152, 0.0722);

#endif

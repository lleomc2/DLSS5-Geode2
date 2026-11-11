// Object-occupancy seed: where the picture has strong colour edges (outlines of blocks, spikes, saws...).
// Input is the 1/4 resolution box-filtered scene; output is a 0..1 edge energy in every channel.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D u_src;
uniform vec2 u_texel;
void main() {
    vec3 cE = texture2D(u_src, v_uv + vec2(u_texel.x, 0.0)).rgb;
    vec3 cW = texture2D(u_src, v_uv - vec2(u_texel.x, 0.0)).rgb;
    vec3 cN = texture2D(u_src, v_uv + vec2(0.0, u_texel.y)).rgb;
    vec3 cS = texture2D(u_src, v_uv - vec2(0.0, u_texel.y)).rgb;
    float g = length(cE - cW) + length(cN - cS);
    float e = smoothstep(0.45, 1.10, g); // only strong outlines count, faint background patterns do not
    gl_FragColor = vec4(e, e, e, 1.0);
}

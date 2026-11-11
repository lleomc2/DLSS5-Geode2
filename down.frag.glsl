#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D u_src;
uniform vec2 u_texel;
uniform vec3 u_pre; // x = threshold, y = knee, z = gain

vec3 prefilter(vec3 c) {
    float lm = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float br = mix(max(max(c.r, c.g), c.b), lm, 0.55);
    float soft = clamp(br - u_pre.x + u_pre.y, 0.0, 2.0 * u_pre.y);
    soft = soft * soft / (4.0 * u_pre.y + 1e-4);
    float contrib = max(soft, br - u_pre.x) / max(br, 1e-4);
    return c * contrib * u_pre.z;
}

vec3 F(vec2 o) {
    vec3 c = texture2D(u_src, v_uv + o * u_texel).rgb;
#ifdef PREFILTER
    c = prefilter(c);
#endif
    return c;
}

void main() {
    vec3 a = F(vec2(-2.0, -2.0)); vec3 b = F(vec2(0.0, -2.0)); vec3 c = F(vec2(2.0, -2.0));
    vec3 d = F(vec2(-2.0,  0.0)); vec3 e = F(vec2(0.0,  0.0)); vec3 f = F(vec2(2.0,  0.0));
    vec3 g = F(vec2(-2.0,  2.0)); vec3 h = F(vec2(0.0,  2.0)); vec3 i = F(vec2(2.0,  2.0));
    vec3 j = F(vec2(-1.0, -1.0)); vec3 k = F(vec2(1.0, -1.0));
    vec3 l = F(vec2(-1.0,  1.0)); vec3 m = F(vec2(1.0,  1.0));
    vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    gl_FragColor = vec4(col, 1.0);
}

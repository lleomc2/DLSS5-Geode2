#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D u_src;
uniform vec2 u_texel;
uniform float u_gain;
void main() {
    vec2 t = u_texel;
    vec3 s = texture2D(u_src, v_uv + vec2(-t.x, -t.y)).rgb
           + texture2D(u_src, v_uv + vec2( 0.0, -t.y)).rgb * 2.0
           + texture2D(u_src, v_uv + vec2( t.x, -t.y)).rgb
           + texture2D(u_src, v_uv + vec2(-t.x,  0.0)).rgb * 2.0
           + texture2D(u_src, v_uv).rgb * 4.0
           + texture2D(u_src, v_uv + vec2( t.x,  0.0)).rgb * 2.0
           + texture2D(u_src, v_uv + vec2(-t.x,  t.y)).rgb
           + texture2D(u_src, v_uv + vec2( 0.0,  t.y)).rgb * 2.0
           + texture2D(u_src, v_uv + vec2( t.x,  t.y)).rgb;
    gl_FragColor = vec4(s * (u_gain / 16.0), 1.0);
}

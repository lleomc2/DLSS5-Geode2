#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D u_src;
uniform vec2 u_texel;
void main() {
    vec3 c = texture2D(u_src, v_uv + u_texel * vec2(-1.0, -1.0)).rgb
           + texture2D(u_src, v_uv + u_texel * vec2( 1.0, -1.0)).rgb
           + texture2D(u_src, v_uv + u_texel * vec2(-1.0,  1.0)).rgb
           + texture2D(u_src, v_uv + u_texel * vec2( 1.0,  1.0)).rgb;
    gl_FragColor = vec4(c * 0.25, 1.0);
}

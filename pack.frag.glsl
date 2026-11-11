// Packs three blur scales of the occupancy map into one texture: r = small, g = medium, b = large.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D u_a;
uniform sampler2D u_b;
uniform sampler2D u_c;
void main() {
    gl_FragColor = vec4(texture2D(u_a, v_uv).r, texture2D(u_b, v_uv).r, texture2D(u_c, v_uv).r, 1.0);
}

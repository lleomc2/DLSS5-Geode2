// Shared helpers for the composite shaders. Prepended to them (after the #defines) at build time.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Hue-preserving highlight shoulder with a white-hot core. Everything up to 0.78 passes through untouched;
// above that the brightest channel rolls off smoothly toward 1.15 (no colour shifts) and very hot pixels
// bleach toward white like an overexposed photograph.
vec3 toneShoulder(vec3 x) {
    const float hi = 0.78;
    const float top = 1.15;
    float m = max(max(x.r, x.g), x.b);
    float t = max(m - hi, 0.0) / (top - hi);
    float e = exp(-2.0 * t);
    float ms = min(m, hi) + (top - hi) * (1.0 - e) / (1.0 + e);
    vec3 y = x * (ms / max(m, 1e-4));
    float hot = smoothstep(0.95, 1.6, m);
    return mix(y, vec3(ms), hot * 0.40);
}

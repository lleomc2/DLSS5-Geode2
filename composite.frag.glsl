// DLSS5 Geode - final composite pass (GLSL ES 1.00).
// Edge-directed AA -> adaptive sharpen -> local clarity -> pseudo-normal relighting (key light + a light
// that follows the player) -> contact shadows / AO -> bloom scatter -> filmic grade -> vignette -> grain.
// Compile with: #define QUALITY 0 (Rendimiento), 1 (Equilibrado) or 2 (Ultra). MAX has its own shader.
// common.glsl (precision, luma, hash12, toneShoulder) is prepended at build time.
#ifndef QUALITY
#define QUALITY 2
#endif
varying vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_base;
uniform sampler2D u_bloom;
uniform vec2  u_texel;
uniform vec2  u_res;
uniform float u_time;
uniform float u_k;           // overall intensity (1.0 = nominal)
uniform float u_detail;      // sharpen + clarity (1.0 = nominal)
uniform float u_split;       // < 0 disabled, otherwise uv.x of the before/after divider
uniform vec4  u_light;       // xy = player position (uv), z = strength, w = radius (uv-y units)
uniform vec3  u_shadowTint;
uniform vec3  u_highTint;
uniform vec3  u_scatter;
uniform vec4  u_mix;         // x = bloom, y = relight, z = shadows/AO, w = grain + chromatic aberration

vec3 S(vec2 uv) { return texture2D(u_scene, uv).rgb; }

void main() {
    vec2 uv = v_uv;
    vec2 t = u_texel;
    float k = u_k;
    float dline = 0.0;
    if (u_split >= 0.0) {
        dline = 1.0 - smoothstep(0.0, t.x * 2.5, abs(uv.x - u_split));
        if (uv.x < u_split) {
            gl_FragColor = vec4(mix(S(uv), vec3(1.0), dline), 1.0);
            return;
        }
    }

    vec2 p = uv - 0.5;
    float aspect = u_res.x / u_res.y;
    float px = u_res.y / 1080.0; // keep the look resolution independent

    // ---- chromatic aberration (Ultra) ---------------------------------------------------
    vec3 c;
#if QUALITY >= 2
    vec2 off = p * (0.0011 * k * u_mix.w) * (0.08 + dot(p, p) * 3.2);
    c = vec3(S(uv + off).r, S(uv).g, S(uv - off).b);
#else
    c = S(uv);
#endif

    vec3 cN = S(uv + vec2(0.0,  t.y));
    vec3 cS = S(uv - vec2(0.0,  t.y));
    vec3 cE = S(uv + vec2(t.x,  0.0));
    vec3 cW = S(uv - vec2(t.x,  0.0));
    float lC = luma(c);
    float lN = luma(cN); float lS = luma(cS); float lE = luma(cE); float lW = luma(cW);

    // ---- edge-directed anti-aliasing ----------------------------------------------------
    vec3 col = c;
#if QUALITY >= 1
    vec2 g = vec2(lE - lW, lN - lS);
    float gl = length(g);
    float edge = smoothstep(0.05, 0.28, gl);
    vec2 dir = (gl > 1e-4) ? vec2(-g.y, g.x) / gl : vec2(1.0, 0.0);
    vec3 a1 = S(uv + dir * t * 0.85);
    vec3 a2 = S(uv - dir * t * 0.85);
    col = mix(c, (c * 2.0 + a1 + a2) * 0.25, edge * 0.9);
#endif

    // ---- adaptive sharpening (contrast adaptive, no halos) ------------------------------
    vec3 mn = min(min(min(cN, cS), min(cE, cW)), col);
    vec3 mx = max(max(max(cN, cS), max(cE, cW)), col);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-3)), 0.0, 1.0));
    vec3 wgt = amp * (-1.0 / mix(9.0, 4.6, clamp(u_detail, 0.0, 1.3) / 1.3));
    col = (col + wgt * (cN + cS + cE + cW)) / (1.0 + 4.0 * wgt);

    // ---- local clarity ("neural texture detail") - luminance only, never clips saturated colours --------------
    vec3 base = texture2D(u_base, uv).rgb;
    float lm = luma(col);
    float midW = 1.0 - abs(lm * 2.0 - 1.0);
    float clar = (lm - luma(base)) * (0.60 * u_detail) * (0.30 + 0.70 * midW) / max(lm, 0.25);
    float room = max(1.0 / max(max(col.r, max(col.g, col.b)), 1e-3), 1.0);
    col *= (clar > 0.0) ? min(1.0 + clar, room) : (1.0 + clar);

// ---- pseudo normals from luminance ----------------------------------------------------
#if QUALITY >= 2
    float lNE = luma(S(uv + t));
    float lNW = luma(S(uv + vec2(-t.x, t.y)));
    float lSE = luma(S(uv + vec2(t.x, -t.y)));
    float lSW = luma(S(uv - t));
    float dx = ((lNE + 2.0 * lE + lSE) - (lNW + 2.0 * lW + lSW)) * 0.25;
    float dy = ((lNW + 2.0 * lN + lNE) - (lSW + 2.0 * lS + lSE)) * 0.25;
#else
    float dx = (lE - lW) * 0.5;
    float dy = (lN - lS) * 0.5;
#endif
    vec3 nrm = normalize(vec3(-dx * 2.6, -dy * 2.6, 1.0));

    // key light: soft, slowly swaying, from the upper left
    float ang = 2.356 + 0.28 * sin(u_time * 0.4);
    vec3 L = normalize(vec3(cos(ang) * 0.78, sin(ang) * 0.78, 0.62));
    vec3 Hh = normalize(L + vec3(0.0, 0.0, 1.0));
    float diff = dot(nrm, L) - L.z;
    float spec = max(pow(max(dot(nrm, Hh), 0.0), 36.0) - pow(Hh.z, 36.0), 0.0);

    // dynamic light that follows the player
    vec2 pa = vec2((uv.x - u_light.x) * aspect, uv.y - u_light.y);
    vec3 lv = vec3(-pa, 0.16);
    float dist = length(lv);
    vec3 Lp = lv / dist;
    float att = 1.0 / (1.0 + (dist * dist) / (u_light.w * u_light.w));
    float pd = (dot(nrm, Lp) - Lp.z) * att * u_light.z;
    vec3 Hp = normalize(Lp + vec3(0.0, 0.0, 1.0));
    float pspec = max(pow(max(dot(nrm, Hp), 0.0), 24.0) - pow(Hp.z, 24.0), 0.0) * att * u_light.z;

    float sat = max(max(col.r, col.g), col.b) - min(min(col.r, col.g), col.b);
    float mat = smoothstep(0.04, 0.42, lm) * (0.55 + 0.45 * sat);
    float rl = u_mix.y * k;
    vec3 lightCol = mix(vec3(1.0), u_highTint, 0.5);
    float shade = diff * 0.62 + pd * 1.25;
    shade = (shade > 0.0) ? shade : shade * (1.0 - 0.75 * lm);   // emissive highlights stay bright
    float room2 = max(1.0 / max(max(col.r, max(col.g, col.b)), 1e-3), 1.0);
    col *= (shade > 0.0) ? min(1.0 + shade * rl * mat, room2) : (1.0 + shade * rl * mat);
    col += lightCol * (spec * 0.75 + pspec * 1.0) * rl * mat * mix(vec3(1.0), col, 0.5);
    col += lightCol * (att * att) * 0.05 * u_light.z * rl * (1.0 - 0.5 * lm); // light spill

    // ---- contact shadows + ambient occlusion ----------------------------------------------
#if QUALITY >= 1
    vec2 ld = normalize(L.xy) * t * px;                       // one 1080p pixel toward the key light
    float bHere = luma(base);
    float soft = luma(texture2D(u_base, uv + ld * 12.0).rgb) - bHere;       // brighter blur toward the light => penumbra
#if QUALITY >= 2
    soft = max(soft, (luma(texture2D(u_base, uv + ld * 26.0).rgb) - bHere) * 0.85);
    float rr = 7.0 * px;
    float ring = luma(texture2D(u_base, uv + vec2( rr * t.x, 0.0)).rgb)
               + luma(texture2D(u_base, uv + vec2(-rr * t.x, 0.0)).rgb)
               + luma(texture2D(u_base, uv + vec2(0.0,  rr * t.y)).rgb)
               + luma(texture2D(u_base, uv + vec2(0.0, -rr * t.y)).rgb);
    float ao = clamp((ring * 0.25 - lC) * 1.3, 0.0, 0.35);
    col *= 1.0 - ao * 0.50 * k * u_mix.z * (1.0 - lm);
#endif
    float contact = max(luma(S(uv + ld * 2.0)) - lC - 0.14, 0.0);
    float shadow = clamp(max(soft, 0.0) * 1.15 + contact * 0.55, 0.0, 0.36) * u_mix.z * k;
    col *= 1.0 - shadow * (1.0 - smoothstep(0.45, 0.90, lm));
#endif

    // ---- bloom / light scattering -------------------------------------------------------------
    vec3 bl = texture2D(u_bloom, uv).rgb;
    col += bl * mix(vec3(1.0), u_scatter, 0.5) * (0.45 * u_mix.x * k);

    // ---- filmic grade -------------------------------------------------------------------------------
    col = max(col, 0.0);
    col = toneShoulder(col);
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.32 * k);               // contrast S-curve
    float l2 = luma(col);
    float s2 = max(max(col.r, col.g), col.b) - min(min(col.r, col.g), col.b);
    col = mix(vec3(l2), col, 1.0 + 0.30 * k * (1.0 - s2));                  // vibrance
    float neutral = 1.0 - smoothstep(0.10, 0.60, s2);                       // keep saturated hues untouched
    float shM = (1.0 - smoothstep(0.0, 0.55, l2)) * (0.35 + 0.65 * neutral);
    float hiM = smoothstep(0.45, 1.0, l2) * neutral;
    col *= 1.0 + (u_shadowTint - 1.0) * shM * 0.60 * k;
    col *= 1.0 + (u_highTint - 1.0) * hiM * 0.45 * k;
    vec2 vp = p * vec2(aspect * 0.62, 1.0);
    col *= 1.0 - 0.24 * k * smoothstep(0.42, 1.05, length(vp) * 1.15);       // vignette

    // ---- grain + dither -----------------------------------------------------------------------------------
    float nz = hash12(gl_FragCoord.xy + fract(u_time * 7.13) * vec2(131.0, 97.0)) - 0.5;
    col += nz * (1.5 / 255.0);
    col += nz * 0.018 * k * u_mix.w * (0.3 + 0.7 * midW);

    col = mix(col, vec3(1.0), dline);
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}

// DLSS5 Geode - MAX composite pass (GLSL ES 1.00).
// Everything Ultra does, rebuilt on better ingredients:
//   FXAA-quality anti-aliasing -> adaptive sharpening -> dual-scale clarity (fine + coarse local contrast)
//   -> bevel normals from a blurred height field + fine normals -> key / fill / rim / player lights with wrap
//   diffuse and glossy specular -> blurred-environment reflections -> screen-space soft shadows and ambient
//   occlusion from an object-occupancy map -> colour-bleeding global illumination -> bloom scatter
//   -> hue-preserving filmic grade -> vignette -> grain + dither.
// common.glsl (precision, luma, hash12, toneShoulder) is prepended at build time.
varying vec2 v_uv;
uniform sampler2D u_scene;   // the frame the game drew
uniform sampler2D u_fine;    // 1/4 res blur (a few pixels)
uniform sampler2D u_wide;    // 1/16 res blur (a few dozen pixels): irradiance
uniform sampler2D u_bloom;   // accumulated bloom
uniform sampler2D u_occ;     // r/g/b = object occupancy at three blur scales
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
uniform vec4  u_fx;          // x = reflections, y = global illumination

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

    // ---- neighbourhood: centre with radial chromatic aberration + 8 neighbours ----------------
    vec2 off = p * (0.0011 * k * u_mix.w) * (0.08 + dot(p, p) * 3.2);
    vec3 cM = vec3(S(uv + off).r, S(uv).g, S(uv - off).b);
    vec3 cN  = S(uv + vec2(0.0,  t.y));
    vec3 cS  = S(uv - vec2(0.0,  t.y));
    vec3 cE  = S(uv + vec2(t.x,  0.0));
    vec3 cW  = S(uv - vec2(t.x,  0.0));
    vec3 cNE = S(uv + t);
    vec3 cNW = S(uv + vec2(-t.x, t.y));
    vec3 cSE = S(uv + vec2(t.x, -t.y));
    vec3 cSW = S(uv - t);
    float lM = luma(cM);
    float lN = luma(cN);  float lS = luma(cS);  float lE = luma(cE);  float lW = luma(cW);
    float lNE = luma(cNE); float lNW = luma(cNW); float lSE = luma(cSE); float lSW = luma(cSW);

    // ---- FXAA (console variant): follows the edge direction, skips flat areas ------------------
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-6.0), vec2(6.0)) * t;
    vec3 rgbA = 0.5 * (S(uv + dir * (1.0 / 3.0 - 0.5)) + S(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (S(uv - dir * 0.5) + S(uv + dir * 0.5));
    float lB = luma(rgbB);
    vec3 aa = (lB < lMin || lB > lMax) ? rgbA : rgbB;
    vec3 col = mix(cM, aa, step(max(0.045, lMax * 0.125), lMax - lMin));

    // ---- adaptive sharpening (contrast adaptive, no halos) -------------------------------------
    vec3 mn = min(min(min(cN, cS), min(cE, cW)), col);
    vec3 mx = max(max(max(cN, cS), max(cE, cW)), col);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-3)), 0.0, 1.0));
    vec3 wgt = amp * (-1.0 / mix(9.0, 4.2, clamp(u_detail, 0.0, 1.3) / 1.3));
    col = (col + wgt * (cN + cS + cE + cW)) / (1.0 + 4.0 * wgt);

    // ---- dual-scale clarity: fine texture detail + coarse local tone mapping --------------------
    vec3 fine = texture2D(u_fine, uv).rgb;
    vec3 wide = texture2D(u_wide, uv).rgb;
    float lm = luma(col);
    float midW = 1.0 - abs(lm * 2.0 - 1.0);
    float clar = ((lm - luma(fine)) * (0.60 * u_detail) * (0.30 + 0.70 * midW)
               + (luma(fine) - luma(wide)) * (0.30 * u_detail) * (0.40 + 0.60 * midW)) / max(lm, 0.25);
    float room = max(1.0 / max(max(col.r, max(col.g, col.b)), 1e-3), 1.0);
    col *= (clar > 0.0) ? min(1.0 + clar, room) : (1.0 + clar);

    // ---- normals: fine (3x3 Sobel) + bevel (gradient of the smooth blurred height field) --------------
    // Faint patterns (background tiles, noise) are ignored on purpose: only real outlines get relief.
    vec2 gF = vec2(((lNE + 2.0 * lE + lSE) - (lNW + 2.0 * lW + lSW)) * 0.25,
                   ((lNW + 2.0 * lN + lNE) - (lSW + 2.0 * lS + lSE)) * 0.25);
    vec2 f6 = t * 9.0;
    vec2 gC = vec2(luma(texture2D(u_fine, uv + vec2(f6.x, 0.0)).rgb) - luma(texture2D(u_fine, uv - vec2(f6.x, 0.0)).rgb),
                   luma(texture2D(u_fine, uv + vec2(0.0, f6.y)).rgb) - luma(texture2D(u_fine, uv - vec2(0.0, f6.y)).rgb));
    float wF = smoothstep(0.03, 0.14, length(gF));
    float wC = smoothstep(0.02, 0.11, length(gC));
    vec3 nrm = normalize(vec3(-(gF * (2.3 * wF) + gC * (2.8 * wC)), 1.0));

    // ---- lights ----------------------------------------------------------------------------------
    float ang = 2.356 + 0.28 * sin(u_time * 0.4);
    vec3 L = normalize(vec3(cos(ang) * 0.78, sin(ang) * 0.78, 0.62));
    vec3 Hh = normalize(L + vec3(0.0, 0.0, 1.0));
    const float wrapD = 0.40;
    float diff = clamp((dot(nrm, L) + wrapD) / (1.0 + wrapD), 0.0, 1.0) - (L.z + wrapD) / (1.0 + wrapD);
    float spec = max(pow(max(dot(nrm, Hh), 0.0), 44.0) - pow(Hh.z, 44.0), 0.0);
    float rim = max(dot(nrm.xy, -normalize(L.xy)), 0.0);        // slopes that face away from the key light

    // light that follows the player
    vec2 pa = vec2((uv.x - u_light.x) * aspect, uv.y - u_light.y);
    vec3 lv = vec3(-pa, 0.16);
    float dist = length(lv);
    vec3 Lp = lv / dist;
    float att = 1.0 / (1.0 + (dist * dist) / (u_light.w * u_light.w));
    float pd = (dot(nrm, Lp) - Lp.z) * att * u_light.z;
    vec3 Hp = normalize(Lp + vec3(0.0, 0.0, 1.0));
    float pspec = max(pow(max(dot(nrm, Hp), 0.0), 26.0) - pow(Hp.z, 26.0), 0.0) * att * u_light.z;

    float sat = max(max(col.r, col.g), col.b) - min(min(col.r, col.g), col.b);
    lm = luma(col);
    float mat = smoothstep(0.04, 0.42, lm) * (0.55 + 0.45 * sat);
    float rl = u_mix.y * k;
    vec3 keyCol = mix(vec3(1.0), u_highTint, 0.5);
    vec3 rimCol = mix(vec3(1.0), u_shadowTint, 0.85);

    float shade = diff * 0.80 + pd * 1.25;
    shade = (shade > 0.0) ? shade : shade * (1.0 - 0.75 * lm);   // emissive highlights never go dark
    room = max(1.0 / max(max(col.r, max(col.g, col.b)), 1e-3), 1.0);
    col *= (shade > 0.0) ? min(1.0 + shade * rl * mat, room) : (1.0 + shade * rl * mat);
    col += keyCol * (spec * 0.80 + pspec * 1.00) * rl * mat * mix(vec3(1.0), col, 0.5);
    col += rimCol * rim * 0.14 * rl * mat;

    // ---- glossy reflections of the blurred surroundings on tilted surfaces --------------------------
    float fres = pow(clamp(1.0 - nrm.z, 0.0, 1.0), 2.0);
    vec3 env = mix(texture2D(u_fine, uv + nrm.xy * vec2(0.07 / aspect, 0.07)).rgb, wide, 0.5);
    col += env * fres * 1.1 * rl * mat * u_fx.x;

    // ---- screen-space soft shadows + ambient occlusion from the occupancy map -------------------------
    // They only fall on background-like pixels (close to the wide blur): objects keep their own colours.
    vec3 occ = texture2D(u_occ, uv).rgb;
    vec2 ld = normalize(L.xy) * t * px;                           // one 1080p pixel toward the key light
    float c1 = texture2D(u_occ, uv + ld * 8.0).r;
    float c2 = min(texture2D(u_occ, uv + ld * 20.0).g * 2.2, 1.0);
    float c3 = min(texture2D(u_occ, uv + ld * 40.0).b * 4.0, 1.0);
    float castShadow = max(c1 * 0.95, max(c2 * 0.70, c3 * 0.40));
    float shadow = clamp((castShadow - occ.r * 0.9) * 1.7, 0.0, 0.45);
    float ao = clamp(min(occ.g * 2.2, 1.0) * 0.30 + min(occ.b * 4.0, 1.0) * 0.12 - occ.r * 0.30, 0.0, 0.28);
    float contact = max(luma(texture2D(u_fine, uv + ld * 3.0).rgb) - luma(fine) - 0.06, 0.0); // smooth, no stair-steps
    float bgLike = 1.0 - smoothstep(0.10, 0.34, length(cM - wide));
    float emissive = smoothstep(0.45, 0.90, lm);
    col *= 1.0 - clamp(shadow * 0.85 + ao * 0.50 + contact * 0.35, 0.0, 0.48) * u_mix.z * k * bgLike * (1.0 - emissive);

    // ---- global illumination: dark surfaces pick up colour from their surroundings ---------------------
    col += wide * (0.12 * u_fx.y * k) * (1.0 - smoothstep(0.12, 0.70, luma(col)));

    // ---- bloom / light scattering -------------------------------------------------------------------
    vec3 bl = texture2D(u_bloom, uv).rgb;
    col += bl * mix(vec3(1.0), u_scatter, 0.5) * (0.45 * u_mix.x * k);

#ifdef BUBBLE_MODE
    // ---- realistic water-like surface ------------------------------------------------------------
    // Screen-space approximation: subtle refraction, flowing ripples and colour-matched reflections.
    // The original Geometry Dash sprite colours remain the material's base colour.
    vec3 waterOcc = texture2D(u_occ, uv).rgb;
    float waterGradLen = length(gF) + 0.35 * length(gC);
    float waterEdge = smoothstep(0.014, 0.105, waterGradLen);
    float waterOccMask = smoothstep(0.010, 0.105, max(waterOcc.r, waterOcc.g * 0.40));
    float waterObject = smoothstep(0.045, 0.22, length(cM - wide));
    float waterMask = clamp(max(waterEdge * 0.72, waterOccMask) * waterObject, 0.0, 1.0);

    vec2 waterGrad = (gF * 0.68 + gC * 0.32) / max(waterGradLen, 0.002);
    vec2 waterPos = uv * vec2(aspect, 1.0);
    float waveA = sin(waterPos.x * 43.0 + sin(waterPos.y * 29.0 + u_time * 0.42) * 1.7 + u_time * 0.31);
    float waveB = sin(waterPos.y * 38.0 + sin(waterPos.x * 25.0 - u_time * 0.34) * 1.4 - u_time * 0.25);
    vec2 waveSlope = vec2(cos(waterPos.x * 43.0 + u_time * 0.31) * 0.035,
                          cos(waterPos.y * 38.0 - u_time * 0.25) * 0.035);
    vec2 waterNormalXY = waterGrad * 0.78 + waveSlope;
    vec3 waterNormal = normalize(vec3(-waterNormalXY * 1.65, 1.0));

    // Small UV displacement bends the scene underneath, like looking through moving water.
    vec2 waterOffset = waterNormalXY * vec2(0.0038 / aspect, 0.0038);
    vec2 waterUV = clamp(uv + waterOffset, vec2(0.001), vec2(0.999));
    vec3 waterRefracted = texture2D(u_scene, waterUV).rgb;
    vec3 waterReflected = texture2D(u_fine, waterUV).rgb;
    vec3 waterSurface = mix(col, waterRefracted, 0.22);
    waterSurface = mix(waterSurface, waterReflected, 0.035);

    float waterSpec = pow(max(dot(waterNormal, normalize(vec3(-0.42, 0.68, 0.82))), 0.0), 24.0);
    float waterFresnel = pow(clamp(1.0 - waterNormal.z, 0.0, 1.0), 2.2);
    float waterRipple = pow(max(waveA * waveB, 0.0), 7.0);
    float waterFoam = pow(max(waveA + waveB - 1.25, 0.0), 3.0);
    vec3 waterHue = clamp(cM, 0.025, 1.0);
    vec3 waterGlint = mix(waterHue, vec3(1.0), 0.24);

    col = mix(col, waterSurface, waterMask * 0.34);
    col += waterGlint * waterMask * (waterSpec * 0.095 + waterFresnel * 0.025);
    col += waterHue * waterMask * waterRipple * 0.025;
    col += waterGlint * waterMask * waterFoam * 0.018;
    col *= 1.0 - waterMask * (0.012 + waterRipple * 0.012);
#endif

    // ---- filmic grade -------------------------------------------------------------------------------
    col = toneShoulder(max(col, 0.0));
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.32 * k);                  // contrast S-curve
    float l2 = luma(col);
    float s2 = max(max(col.r, col.g), col.b) - min(min(col.r, col.g), col.b);
    col = mix(vec3(l2), col, 1.0 + 0.30 * k * (1.0 - s2));                     // vibrance
    float neutral = 1.0 - smoothstep(0.10, 0.60, s2);                          // keep saturated hues untouched
    float shM = (1.0 - smoothstep(0.0, 0.55, l2)) * (0.35 + 0.65 * neutral);
    float hiM = smoothstep(0.45, 1.0, l2) * neutral;
    col *= 1.0 + (u_shadowTint - 1.0) * shM * 0.60 * k;
    col *= 1.0 + (u_highTint - 1.0) * hiM * 0.45 * k;
    vec2 vp = p * vec2(aspect * 0.62, 1.0);
    col *= 1.0 - 0.24 * k * smoothstep(0.42, 1.05, length(vp) * 1.15);          // vignette

    // ---- grain + dither -----------------------------------------------------------------------------
    float nz = hash12(gl_FragCoord.xy + fract(u_time * 7.13) * vec2(131.0, 97.0)) - 0.5;
    col += nz * (1.5 / 255.0);
    col += nz * 0.018 * k * u_mix.w * (0.3 + 0.7 * midW);

    col = mix(col, vec3(1.0), dline);
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}

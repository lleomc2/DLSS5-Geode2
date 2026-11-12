#include "Pipeline.hpp"

#include "Config.hpp"
#include "Shaders.hpp"

#include <Geode/Geode.hpp>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>

using namespace geode::prelude;

namespace dlss5 {
namespace {

constexpr int kBloomTargets = 5; // 1/4 .. 1/64 resolution
constexpr int kUnits = 6;        // texture units whose bindings we preserve (the MAX composite uses 5)

struct Target {
    GLuint tex = 0;
    GLuint fbo = 0;
    int w = 0;
    int h = 0;
};

struct Program {
    GLuint id = 0;
    // generic passes
    GLint uSrc = -1, uTexel = -1, uPre = -1, uGain = -1;
    // pack pass
    GLint uA = -1, uB = -1, uC = -1;
    // composite
    GLint uScene = -1, uBase = -1, uBloom = -1, uFine = -1, uWide = -1, uOcc = -1, uRes = -1, uTime = -1;
    GLint uK = -1, uDetail = -1, uSplit = -1, uLight = -1, uShadowTint = -1, uHighTint = -1, uScatter = -1;
    GLint uMix = -1, uFx = -1;
};

struct Style {
    float shadow[3];
    float high[3];
    float scatter[3];
};

// Order must match the "look" setting: Cinematico, Natural, Calido, Frio, Neon
constexpr Style kStyles[] = {
    {{0.88f, 0.98f, 1.12f}, {1.10f, 1.02f, 0.90f}, {1.00f, 0.93f, 0.84f}},
    {{1.00f, 1.00f, 1.00f}, {1.00f, 1.00f, 1.00f}, {1.00f, 1.00f, 1.00f}},
    {{1.03f, 0.98f, 0.93f}, {1.13f, 1.03f, 0.86f}, {1.00f, 0.90f, 0.76f}},
    {{0.88f, 0.98f, 1.14f}, {0.95f, 1.02f, 1.10f}, {0.84f, 0.94f, 1.00f}},
    {{0.92f, 0.84f, 1.20f}, {0.92f, 1.10f, 1.14f}, {0.82f, 0.92f, 1.16f}},
};

enum class Res { Ok, TierFailed, Fatal };

struct Pipe {
    bool failed = false;
    bool programsReady = false;
    bool maxProgramsReady = false;
    bool experimentalProgramReady = false;
    bool bubbleProgramReady = false;
    bool waterProgramReady = false;
    bool maxTargetsReady = false;
    void* ctx = nullptr;
    unsigned lastFrame = 0xFFFFFFFFu;
    int errorFrames = 0;
    bool fmtSwapTried = false;
    bool announced = false;

    // quality tiers: hwCap = best tier this GPU compiled, fpsCap = lowered by the FPS guard
    int hwCap = kTierCount - 1;
    int fpsCap = kTierCount - 1;
    int lastRequested = -1;
    int lastEffective = 1;

    int w = 0, h = 0;
    GLenum sceneFmt = GL_RGBA;
    GLuint vbo = 0;
    GLuint sceneTex = 0;
    Target base1, base2, base3, fineS;
    Target occA, occB, occC, occ;
    std::array<Target, kBloomTargets> bloom;

    Program box, down, downPre, up, edge, pack, experimental, bubble, water;
    std::array<Program, kTierCount> comp;
    std::array<bool, kTierCount> compReady{false, false, false, false};

    float lightX = 0.2f, lightY = 0.25f, lightStrength = 0.f;

    // FPS guard
    std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
    bool haveLastFrame = false;
    float guardAccum = 0.f;
    int guardFrames = 0;
    int lowWindows = 0;

    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
};

Pipe g;

// ---------------------------------------------------------------------------------------------
// GL state save / restore. cocos2d caches program, textures and blend state, so everything we
// touch has to go back exactly as we found it or the next game frame would render garbage.
// ---------------------------------------------------------------------------------------------
struct StateGuard {
    GLint program = 0, fbo = 0, arrayBuf = 0, activeTex = GL_TEXTURE0;
    GLint tex[kUnits] = {};
    GLint viewport[4] = {0, 0, 0, 0};
    GLboolean blend = GL_FALSE, depth = GL_FALSE, cull = GL_FALSE, scissor = GL_FALSE, stencil = GL_FALSE;
    GLint srcRGB = GL_ONE, dstRGB = GL_ZERO, srcA = GL_ONE, dstA = GL_ZERO;
    GLint eqRGB = GL_FUNC_ADD, eqA = GL_FUNC_ADD;
    GLboolean cmask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint attrib[5] = {0, 0, 0, 0, 0};

    StateGuard() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        for (int i = 0; i < kUnits; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex[i]);
        }
        glGetIntegerv(GL_VIEWPORT, viewport);
        blend = glIsEnabled(GL_BLEND);
        depth = glIsEnabled(GL_DEPTH_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        stencil = glIsEnabled(GL_STENCIL_TEST);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqA);
        glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
        for (int i = 0; i < 5; ++i) {
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib[i]);
        }
    }

    ~StateGuard() {
        for (int i = 0; i < 5; ++i) {
            if (attrib[i]) glEnableVertexAttribArray(i);
            else glDisableVertexAttribArray(i);
        }
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuf));
        glUseProgram(static_cast<GLuint>(program));
        for (int i = 0; i < kUnits; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex[i]));
        }
        glActiveTexture(static_cast<GLenum>(activeTex));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
        glBlendEquationSeparate(static_cast<GLenum>(eqRGB), static_cast<GLenum>(eqA));
        glBlendFuncSeparate(static_cast<GLenum>(srcRGB), static_cast<GLenum>(dstRGB),
                            static_cast<GLenum>(srcA), static_cast<GLenum>(dstA));
        auto set = [](GLenum cap, GLboolean on) {
            if (on) glEnable(cap);
            else glDisable(cap);
        };
        set(GL_BLEND, blend);
        set(GL_DEPTH_TEST, depth);
        set(GL_CULL_FACE, cull);
        set(GL_SCISSOR_TEST, scissor);
        set(GL_STENCIL_TEST, stencil);
    }
};

void drainErrors() {
    for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) {
    }
}

void notify(std::string text) {
    Loader::get()->queueInMainThread([text = std::move(text)] {
        Notification::create(text, NotificationIcon::Warning, 3.5f)->show();
    });
}

// ---------------------------------------------------------------------------------------------
// Shader / target helpers
// ---------------------------------------------------------------------------------------------
GLuint compileShader(GLenum type, std::string const& src, std::string& err) {
    GLuint s = glCreateShader(type);
    if (!s) {
        err = "glCreateShader fallo";
        return 0;
    }
    char const* ptr = src.c_str();
    glShaderSource(s, 1, &ptr, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024] = {0};
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(buf) - 1, &n, buf);
        err.assign(buf, static_cast<size_t>(std::max<GLsizei>(n, 0)));
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// source = defines + (common ? kCommon : "") + body
bool buildProgram(Program& p, char const* body, std::string const& defines, std::string& err, bool common = false) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, std::string(shaders::kVert), err);
    if (!vs) return false;
    std::string src = defines;
    if (common) src += shaders::kCommon;
    src += body;
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, src, err);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    GLuint id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glBindAttribLocation(id, 0, "a_pos");
    glLinkProgram(id);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024] = {0};
        GLsizei n = 0;
        glGetProgramInfoLog(id, sizeof(buf) - 1, &n, buf);
        err.assign(buf, static_cast<size_t>(std::max<GLsizei>(n, 0)));
        glDeleteProgram(id);
        return false;
    }
    p = Program{};
    p.id = id;
    auto loc = [&](char const* name) { return glGetUniformLocation(id, name); };
    p.uSrc = loc("u_src");
    p.uTexel = loc("u_texel");
    p.uPre = loc("u_pre");
    p.uGain = loc("u_gain");
    p.uA = loc("u_a");
    p.uB = loc("u_b");
    p.uC = loc("u_c");
    p.uScene = loc("u_scene");
    p.uBase = loc("u_base");
    p.uBloom = loc("u_bloom");
    p.uFine = loc("u_fine");
    p.uWide = loc("u_wide");
    p.uOcc = loc("u_occ");
    p.uRes = loc("u_res");
    p.uTime = loc("u_time");
    p.uK = loc("u_k");
    p.uDetail = loc("u_detail");
    p.uSplit = loc("u_split");
    p.uLight = loc("u_light");
    p.uShadowTint = loc("u_shadowTint");
    p.uHighTint = loc("u_highTint");
    p.uScatter = loc("u_scatter");
    p.uMix = loc("u_mix");
    p.uFx = loc("u_fx");
    return true;
}

void setLinearClamp() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

bool makeTarget(Target& t, int w, int h, std::string& err) {
    t.w = std::max(1, w);
    t.h = std::max(1, h);
    glGenTextures(1, &t.tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    setLinearClamp();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        err = "framebuffer incompleto (0x" + std::to_string(status) + ") " + std::to_string(t.w) + "x" + std::to_string(t.h);
        return false;
    }
    return true;
}

void destroyTarget(Target& t, bool deleteGL) {
    if (deleteGL) {
        if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
        if (t.tex) glDeleteTextures(1, &t.tex);
    }
    t = Target{};
}

void destroyMaxTargets(bool deleteGL) {
    destroyTarget(g.base3, deleteGL);
    destroyTarget(g.fineS, deleteGL);
    destroyTarget(g.occA, deleteGL);
    destroyTarget(g.occB, deleteGL);
    destroyTarget(g.occC, deleteGL);
    destroyTarget(g.occ, deleteGL);
    g.maxTargetsReady = false;
}

void destroyTargets(bool deleteGL) {
    destroyMaxTargets(deleteGL);
    destroyTarget(g.base1, deleteGL);
    destroyTarget(g.base2, deleteGL);
    for (auto& b : g.bloom) destroyTarget(b, deleteGL);
    if (g.sceneTex && deleteGL) glDeleteTextures(1, &g.sceneTex);
    g.sceneTex = 0;
    g.w = g.h = 0;
}

void releaseAll(bool deleteGL) {
    destroyTargets(deleteGL);
    auto kill = [&](Program& p) {
        if (deleteGL && p.id) glDeleteProgram(p.id);
        p = Program{};
    };
    kill(g.box);
    kill(g.down);
    kill(g.downPre);
    kill(g.up);
    kill(g.edge);
    kill(g.pack);
    kill(g.experimental);
    kill(g.bubble);
    kill(g.water);
    g.experimentalProgramReady = false;
    g.bubbleProgramReady = false;
    g.waterProgramReady = false;
    for (auto& p : g.comp) kill(p);
    g.compReady = {false, false, false, false};
    if (deleteGL && g.vbo) glDeleteBuffers(1, &g.vbo);
    g.vbo = 0;
    g.programsReady = false;
    g.maxProgramsReady = false;
}

// After an Android pause/resume the EGL context is usually recreated. We notice that through the context
// pointer, and as a second line of defence we periodically verify that our framebuffers still exist.
bool targetsValid() {
    if (!g.base1.fbo || !g.sceneTex) return true; // nothing created yet
    if (!glIsTexture(g.sceneTex) || !glIsFramebuffer(g.base1.fbo)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, g.base1.fbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    GLint name = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    return name == static_cast<GLint>(g.base1.tex);
}

void fail(std::string const& why) {
    log::error("DLSS5: filtro desactivado -> {}", why);
    releaseAll(true);
    g.failed = true;
    notify("DLSS 5: el filtro se desactivo por un error de GPU (ver log)");
}

// This GPU could not run tier q: remember it and fall back to the next one down.
void tierDown(int q, std::string const& why) {
    g.hwCap = std::min(g.hwCap, q - 1);
    log::warn("DLSS5: tu GPU no puede con {} ({}), usando {}", qualityName(q), why, qualityName(g.hwCap));
    notify(std::string("DLSS 5: tu GPU no soporta ") + qualityName(q) + ", usando " + qualityName(g.hwCap));
}

Res ensureResources(int w, int h, int q, std::string& err) {
    if (!g.programsReady) {
        using namespace shaders;
        if (!buildProgram(g.box, kFragBox, "", err)) return Res::Fatal;
        if (!buildProgram(g.down, kFragDown, "", err)) return Res::Fatal;
        if (!buildProgram(g.downPre, kFragDown, "#define PREFILTER 1\n", err)) return Res::Fatal;
        if (!buildProgram(g.up, kFragUp, "", err)) return Res::Fatal;
        static GLfloat const quad[8] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
        glGenBuffers(1, &g.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        g.programsReady = true;
    }
    if (!g.compReady[q]) {
#ifdef DLSS5_TEST_FAIL_TIER
        if (q == DLSS5_TEST_FAIL_TIER) { // test hook: pretend this GPU cannot compile the tier
            err = "fallo forzado (prueba)";
            tierDown(q, err);
            return Res::TierFailed;
        }
#endif
        std::string defs = "#define QUALITY " + std::to_string(q) + "\n";
        char const* body = (q == 3) ? shaders::kFragCompositeMax : shaders::kFragComposite;
        if (!buildProgram(g.comp[q], body, defs, err, true)) {
            if (q == 0) return Res::Fatal;
            tierDown(q, err);
            return Res::TierFailed;
        }
        g.compReady[q] = true;
    }
    if (q == 3 && !g.maxProgramsReady) {
        if (!buildProgram(g.edge, shaders::kFragEdge, "", err) || !buildProgram(g.pack, shaders::kFragPack, "", err)) {
            tierDown(q, err);
            return Res::TierFailed;
        }
        g.maxProgramsReady = true;
    }
    if (q == 3 && config().experimental.load() && !g.experimentalProgramReady) {
        std::string defs = "#define QUALITY 3\n#define EXPERIMENTAL_BUBBLES 1\n";
        if (!buildProgram(g.experimental, shaders::kFragCompositeMax, defs, err, true)) {
            notify("DLSS 5 Experimental: no se pudo compilar el efecto; usando MAX");
            config().experimental = false;
            return Res::Ok;
        }
        g.experimentalProgramReady = true;
    }
    if (q == 3 && config().bubble.load() && !g.bubbleProgramReady) {
        std::string defs = "#define QUALITY 3\n#define EXPERIMENTAL_BUBBLES 1\n#define BUBBLE_MODE 1\n";
        if (!buildProgram(g.bubble, shaders::kFragCompositeMax, defs, err, true)) {
            notify("DLSS 5 BURBUJA: no se pudo compilar el efecto; usando MAX");
            config().bubble = false;
            return Res::Ok;
        }
        g.bubbleProgramReady = true;
    }
    if (q == 3 && config().water.load() && !g.waterProgramReady) {
        std::string defs = "#define QUALITY 3\n#define BUBBLE_MODE 1\n";
        if (!buildProgram(g.water, shaders::kFragCompositeMax, defs, err, true)) {
            notify("DLSS 5 AGUA REALISTA: no se pudo compilar el efecto; usando MAX");
            config().water = false;
            return Res::Ok;
        }
        g.waterProgramReady = true;
    }
    if (w != g.w || h != g.h || !g.sceneTex) {
        destroyTargets(true);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        GLint alphaBits = 0;
        glGetIntegerv(GL_ALPHA_BITS, &alphaBits);
        if (!g.fmtSwapTried) g.sceneFmt = alphaBits > 0 ? GL_RGBA : GL_RGB;
        glGenTextures(1, &g.sceneTex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g.sceneTex);
        setLinearClamp();
        glTexImage2D(GL_TEXTURE_2D, 0, g.sceneFmt, w, h, 0, g.sceneFmt, GL_UNSIGNED_BYTE, nullptr);
        if (!makeTarget(g.base1, (w + 3) / 4, (h + 3) / 4, err)) return Res::Fatal;
        if (!makeTarget(g.base2, (w + 7) / 8, (h + 7) / 8, err)) return Res::Fatal;
        for (int i = 0; i < kBloomTargets; ++i) {
            int d = 4 << i;
            if (!makeTarget(g.bloom[i], (w + d - 1) / d, (h + d - 1) / d, err)) return Res::Fatal;
        }
        g.w = w;
        g.h = h;
        log::info("DLSS5: recursos listos {}x{} (formato de captura {}), GPU: {}", w, h,
                  g.sceneFmt == GL_RGBA ? "RGBA" : "RGB",
                  reinterpret_cast<char const*>(glGetString(GL_RENDERER)));
    }
    if (q == 3 && !g.maxTargetsReady) {
        bool ok = makeTarget(g.base3, (w + 15) / 16, (h + 15) / 16, err)
               && makeTarget(g.fineS, (w + 3) / 4, (h + 3) / 4, err)
               && makeTarget(g.occA, (w + 3) / 4, (h + 3) / 4, err)
               && makeTarget(g.occB, (w + 7) / 8, (h + 7) / 8, err)
               && makeTarget(g.occC, (w + 15) / 16, (h + 15) / 16, err)
               && makeTarget(g.occ, (w + 3) / 4, (h + 3) / 4, err);
        if (!ok) {
            destroyMaxTargets(true);
            tierDown(q, err);
            return Res::TierFailed;
        }
        g.maxTargetsReady = true;
    }
    return Res::Ok;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------
void bindTex(int unit, GLuint tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

void runPass(Program const& p, GLuint srcTex, int srcW, int srcH, Target const& dst, float gain,
             float const* pre, bool additive) {
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    glViewport(0, 0, dst.w, dst.h);
    glUseProgram(p.id);
    bindTex(0, srcTex);
    glUniform1i(p.uSrc, 0);
    glUniform2f(p.uTexel, 1.f / static_cast<float>(srcW), 1.f / static_cast<float>(srcH));
    if (p.uGain >= 0) glUniform1f(p.uGain, gain);
    if (p.uPre >= 0 && pre) glUniform3f(p.uPre, pre[0], pre[1], pre[2]);
    if (additive) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glDisable(GL_BLEND);
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void updateLight() {
    float target = 0.f;
    float ux = g.lightX, uy = g.lightY;
    if (config().playerLight.load()) {
        if (auto* pl = PlayLayer::get()) {
            if (auto* p = pl->m_player1) {
                if (p->isVisible() && p->getParent()) {
                    CCPoint wp = p->getParent()->convertToWorldSpace(p->getPosition());
                    CCSize ws = CCDirector::sharedDirector()->getWinSize();
                    if (ws.width > 1.f && ws.height > 1.f) {
                        ux = std::clamp(wp.x / ws.width, -0.25f, 1.25f);
                        uy = std::clamp(wp.y / ws.height, -0.25f, 1.25f);
                        target = 1.f;
                    }
                }
            }
        }
    }
    if (g.lightStrength < 0.02f && target > 0.f) {
        g.lightX = ux;
        g.lightY = uy;
    } else {
        g.lightX += (ux - g.lightX) * 0.45f;
        g.lightY += (uy - g.lightY) * 0.45f;
    }
    g.lightStrength += (target - g.lightStrength) * 0.10f;
}

// Optional protection: if the game stays well below its target frame rate for a few seconds while the
// filter runs, step the quality down one tier. Resets when the user picks a quality again.
void fpsGuardTick() {
    auto now = std::chrono::steady_clock::now();
    if (g.haveLastFrame) {
        float dt = std::chrono::duration<float>(now - g.lastFrameTime).count();
        if (dt < 1.0f) { // a gap this long is loading or app switching, not frame rate
            g.guardAccum += std::min(dt, 0.25f);
            ++g.guardFrames;
        }
    }
    g.lastFrameTime = now;
    g.haveLastFrame = true;
    if (g.guardAccum < 2.0f || g.guardFrames < 10) return;

    float avg = g.guardAccum / static_cast<float>(g.guardFrames);
    g.guardAccum = 0.f;
    g.guardFrames = 0;
    double target = CCDirector::sharedDirector()->getAnimationInterval();
    if (!(target > 0.001 && target < 0.1)) target = 1.0 / 60.0;
    float limit = std::max(1.f / 42.f, 1.35f * static_cast<float>(target));
    g.lowWindows = (avg > limit) ? g.lowWindows + 1 : 0;
    if (g.lowWindows >= 2 && g.lastEffective > 0) {
        g.lowWindows = 0;
        g.fpsCap = g.lastEffective - 1;
        log::info("DLSS5: FPS bajos ({:.0f}), bajando a {}", 1.f / avg, qualityName(g.fpsCap));
        notify(std::string("DLSS 5: calidad bajada a ") + qualityName(g.fpsCap) + " por FPS bajos");
    }
}

} // namespace

void onFrameRendered() {
    if (g.failed) return;
    initConfig();
    auto& c = config();
    if (!c.enabled.load() || c.intensity.load() < 0.002f) {
        g.haveLastFrame = false;
        return;
    }

    auto* director = CCDirector::sharedDirector();
    if (!director) return;
    unsigned frame = director->getTotalFrames();
    if (frame == g.lastFrame) return; // already handled (two hooks can report the same frame)
    g.lastFrame = frame;

    bool inLevel = PlayLayer::get() != nullptr || LevelEditorLayer::get() != nullptr;
    if (!c.menus.load() && !inLevel) {
        g.haveLastFrame = false;
        return;
    }

    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLSurface surf = eglGetCurrentSurface(EGL_DRAW);
    EGLContext ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT) return;
    EGLint sw = 0, sh = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &sw);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &sh);
    if (sw < 32 || sh < 32) return;

    if (g.ctx != ctx) {
        if (g.ctx) log::info("DLSS5: contexto GL nuevo, recreando recursos");
        releaseAll(false); // old handles died with the old context
        g.ctx = ctx;
    }

    // ---- quality tier ----------------------------------------------------------------------------
    int requested = std::clamp(c.quality.load(), 0, kTierCount - 1);
    if (requested != g.lastRequested) {
        g.lastRequested = requested;
        g.fpsCap = kTierCount - 1; // the user chose: give the full tier another chance
        g.lowWindows = 0;
        g.guardAccum = 0.f;
        g.guardFrames = 0;
    }
    if (c.autoQuality.load()) fpsGuardTick();
    else g.haveLastFrame = false;
    int q = std::min({requested, g.hwCap, g.fpsCap});

    drainErrors();
    StateGuard guard; // restores GL state when this function returns

    if (g.programsReady && g.w > 0 && (frame % 10u) == 0u && !targetsValid()) {
        log::warn("DLSS5: los recursos GL ya no son validos, recreando");
        releaseAll(false);
        drainErrors();
    }

    std::string err;
    Res r;
    while ((r = ensureResources(sw, sh, q, err)) == Res::TierFailed) {
        q = std::min(q, g.hwCap);
        err.clear();
    }
    if (r == Res::Fatal) {
        fail(err.empty() ? "no se pudieron crear los recursos" : err);
        return;
    }
    g.lastEffective = q;

    // ---- common state -----------------------------------------------------------------------------
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    for (int i = 1; i < 5; ++i) glDisableVertexAttribArray(i);

    // ---- capture the finished frame -------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bindTex(0, g.sceneTex);
    drainErrors();
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, sw, sh);
    if (glGetError() != GL_NO_ERROR) {
        if (!targetsValid()) {
            log::warn("DLSS5: los recursos GL ya no son validos, recreando");
            releaseAll(false);
            drainErrors();
            return;
        }
        if (!g.fmtSwapTried) {
            // the surface and the capture texture disagree about alpha: retry once with the other format
            g.fmtSwapTried = true;
            g.sceneFmt = (g.sceneFmt == GL_RGBA) ? GL_RGB : GL_RGBA;
            destroyTargets(true);
            log::warn("DLSS5: la captura fallo, reintentando con otro formato");
            return;
        }
        fail("glCopyTexSubImage2D fallo");
        return;
    }

    float const pre[3] = {0.70f, 0.22f, 1.00f};
    float const noPre[3] = {0.f, 0.f, 1.f};
    int const levels = (q == 0) ? 3 : (q == 3 ? 5 : 4);

    // ---- blurred copies of the scene (local clarity, ambient occlusion, irradiance) ----------------
    runPass(g.box, g.sceneTex, sw, sh, g.base1, 1.f, nullptr, false);
    runPass(g.down, g.base1.tex, g.base1.w, g.base1.h, g.base2, 1.f, noPre, false);
    GLuint baseTex = g.base2.tex;
    if (q == 3) {
        runPass(g.down, g.base2.tex, g.base2.w, g.base2.h, g.base3, 1.f, noPre, false);
        runPass(g.up, g.base2.tex, g.base2.w, g.base2.h, g.fineS, 1.f, nullptr, false); // smooth 1/4-res blur
    } else if (q >= 1) {
        runPass(g.up, g.base2.tex, g.base2.w, g.base2.h, g.base1, 1.f, nullptr, false);
        baseTex = g.base1.tex;
    }

    // ---- bloom chain --------------------------------------------------------------------------------
    runPass(g.downPre, g.sceneTex, sw, sh, g.bloom[0], 1.f, pre, false);
    for (int i = 1; i < levels; ++i) {
        runPass(g.down, g.bloom[i - 1].tex, g.bloom[i - 1].w, g.bloom[i - 1].h, g.bloom[i], 1.f, noPre, false);
    }
    for (int i = levels - 1; i > 0; --i) {
        runPass(g.up, g.bloom[i].tex, g.bloom[i].w, g.bloom[i].h, g.bloom[i - 1], 1.f, nullptr, true);
    }

    // ---- MAX: object-occupancy map (soft shadows + ambient occlusion) --------------------------------
    if (q == 3) {
        runPass(g.edge, g.base1.tex, g.base1.w, g.base1.h, g.occA, 1.f, nullptr, false);
        runPass(g.down, g.occA.tex, g.occA.w, g.occA.h, g.occB, 1.f, noPre, false);
        runPass(g.down, g.occB.tex, g.occB.w, g.occB.h, g.occC, 1.f, noPre, false);
        glBindFramebuffer(GL_FRAMEBUFFER, g.occ.fbo);
        glViewport(0, 0, g.occ.w, g.occ.h);
        glDisable(GL_BLEND);
        glUseProgram(g.pack.id);
        bindTex(0, g.occA.tex);
        bindTex(1, g.occB.tex);
        bindTex(2, g.occC.tex);
        glUniform1i(g.pack.uA, 0);
        glUniform1i(g.pack.uB, 1);
        glUniform1i(g.pack.uC, 2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // ---- final composite straight into the screen ----------------------------------------------------
    updateLight();
    Program const& cp = (q == 3 && c.water.load() && g.waterProgramReady) ? g.water
                      : (q == 3 && c.bubble.load() && g.bubbleProgramReady) ? g.bubble
                      : (q == 3 && c.experimental.load() && g.experimentalProgramReady) ? g.experimental
                      : g.comp[q];
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, sw, sh);
    glDisable(GL_BLEND);
    glUseProgram(cp.id);
    bindTex(0, g.sceneTex);
    glUniform1i(cp.uScene, 0);
    if (q == 3) {
        bindTex(1, g.fineS.tex);
        bindTex(2, g.base3.tex);
        bindTex(3, g.bloom[0].tex);
        bindTex(4, g.occ.tex);
        glUniform1i(cp.uFine, 1);
        glUniform1i(cp.uWide, 2);
        glUniform1i(cp.uBloom, 3);
        glUniform1i(cp.uOcc, 4);
        glUniform4f(cp.uFx, c.reflect.load(), c.gi.load(), 0.f, 0.f);
    } else {
        bindTex(1, baseTex);
        bindTex(2, g.bloom[0].tex);
        glUniform1i(cp.uBase, 1);
        glUniform1i(cp.uBloom, 2);
    }

    Style const& st = kStyles[std::clamp(c.look.load(), 0, 4)];
    float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - g.t0).count();
    glUniform2f(cp.uTexel, 1.f / static_cast<float>(sw), 1.f / static_cast<float>(sh));
    glUniform2f(cp.uRes, static_cast<float>(sw), static_cast<float>(sh));
    glUniform1f(cp.uTime, std::fmod(seconds, 1000.f));
    glUniform1f(cp.uK, c.intensity.load());
    glUniform1f(cp.uDetail, c.detail.load());
    glUniform1f(cp.uSplit, c.compare.load() ? c.comparePos.load() : -1.f);
    glUniform4f(cp.uLight, g.lightX, g.lightY, g.lightStrength, 0.35f);
    glUniform3f(cp.uShadowTint, st.shadow[0], st.shadow[1], st.shadow[2]);
    glUniform3f(cp.uHighTint, st.high[0], st.high[1], st.high[2]);
    glUniform3f(cp.uScatter, st.scatter[0], st.scatter[1], st.scatter[2]);
    glUniform4f(cp.uMix, c.bloom.load(), c.relight.load(), c.shadows.load(), c.film.load());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // ---- error watchdog ------------------------------------------------------------------------------
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        drainErrors();
        ++g.errorFrames;
        if (!targetsValid()) {
            // our objects vanished (context recreated behind our back): rebuild everything next frame
            log::warn("DLSS5: los recursos GL ya no son validos, recreando");
            releaseAll(false);
            g.errorFrames = 0;
        } else if (g.errorFrames >= 30) {
            fail("errores GL repetidos (ultimo 0x" + std::to_string(e) + ")");
            return;
        }
    } else {
        g.errorFrames = 0;
    }

    if (!g.announced) {
        g.announced = true;
        log::info("DLSS5: filtro activo (calidad {}, {}x{})", qualityName(q), sw, sh);
    }
}

} // namespace dlss5

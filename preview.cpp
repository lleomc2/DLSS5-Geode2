// Desktop preview + self-test for DLSS5 Geode. It compiles the REAL src/Pipeline.cpp against a tiny
// Geode shim and runs it on an offscreen OpenGL ES 2 context (Mesa software rasteriser).
//
//   preview render <in.rgba> <out.rgba> <w> <h> <quality 0-3> <look 0-4> <frames> [key=value ...]
//   preview selftest <in.rgba> <w> <h>
//
// key=value: intensity detail bloom relight shadows film reflect gi (percent, 100 = default), light=0|1
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Config.hpp"
#include "Pipeline.hpp"
#include <Geode/Geode.hpp>

namespace dlss5 {
Config& config() { static Config c; return c; }
void initConfig() {}
} // namespace dlss5

static int W = 1600, H = 720;
static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  CHECK FAILED: %s\n", msg); ++fails; } } while (0)

static GLuint mkProg(const char* vs, const char* fs) {
    auto sh = [](GLenum t, const char* s) { GLuint x = glCreateShader(t); glShaderSource(x, 1, &s, nullptr); glCompileShader(x); GLint ok; glGetShaderiv(x, GL_COMPILE_STATUS, &ok); if (!ok) { char b[512]; glGetShaderInfoLog(x, 512, nullptr, b); std::printf("blit shader error: %s\n", b); std::exit(2);} return x; };
    GLuint p = glCreateProgram();
    glAttachShader(p, sh(GL_VERTEX_SHADER, vs));
    glAttachShader(p, sh(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    return p;
}

static std::vector<unsigned char> readFile(const char* path, size_t n) {
    std::vector<unsigned char> d(n);
    FILE* f = std::fopen(path, "rb");
    if (!f || std::fread(d.data(), 1, n, f) != n) { std::printf("cannot read %s\n", path); std::exit(3); }
    std::fclose(f);
    return d;
}

struct Snapshot {
    GLint program, fbo, arrayBuf, activeTex, tex[6], vp[4], srcRGB, dstRGB, srcA, dstA, eqRGB, eqA, attrib[5];
    GLboolean blend, depth, cull, scissor, stencil, cmask[4];
    bool operator==(const Snapshot& o) const { return std::memcmp(this, &o, sizeof(Snapshot)) == 0; }
};
static Snapshot snap() {
    Snapshot s{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTex);
    for (int i = 0; i < 6; ++i) { glActiveTexture(GL_TEXTURE0 + i); glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex[i]); }
    glActiveTexture(s.activeTex);
    glGetIntegerv(GL_VIEWPORT, s.vp);
    glGetIntegerv(GL_BLEND_SRC_RGB, &s.srcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &s.dstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.srcA); glGetIntegerv(GL_BLEND_DST_ALPHA, &s.dstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &s.eqRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.eqA);
    for (int i = 0; i < 5; ++i) glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s.attrib[i]);
    s.blend = glIsEnabled(GL_BLEND); s.depth = glIsEnabled(GL_DEPTH_TEST); s.cull = glIsEnabled(GL_CULL_FACE);
    s.scissor = glIsEnabled(GL_SCISSOR_TEST); s.stencil = glIsEnabled(GL_STENCIL_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, s.cmask);
    return s;
}

struct Gfx {
    EGLDisplay dpy; EGLSurface surf; EGLContext ctx; EGLConfig cfg;
    GLuint sceneTex = 0, blit = 0, vbo = 0;
    std::vector<unsigned char> scene;
    EGLint ctxAttr[3] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    bool init() {
        setenv("EGL_PLATFORM", "surfaceless", 1);
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint maj, mnr;
        if (!eglInitialize(dpy, &maj, &mnr)) { std::puts("eglInitialize failed"); return false; }
        eglBindAPI(EGL_OPENGL_ES_API);
        EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLint n = 0;
        if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n) { std::puts("no EGL config"); return false; }
        EGLint pb[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
        surf = eglCreatePbufferSurface(dpy, cfg, pb);
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
        if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, surf, surf, ctx)) { std::puts("EGL setup failed"); return false; }
        return true;
    }
    void setupGL() {
        glGenTextures(1, &sceneTex); glBindTexture(GL_TEXTURE_2D, sceneTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, scene.data());
        blit = mkProg("attribute vec2 a_pos; varying vec2 v; void main(){ v=a_pos*0.5+0.5; gl_Position=vec4(a_pos,0.,1.); }",
                      "precision mediump float; varying vec2 v; uniform sampler2D t; void main(){ gl_FragColor=texture2D(t,v); }");
        GLfloat quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
        glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    }
    void drawGameFrame() {   // what the game itself would have drawn
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, W, H);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glColorMask(1, 1, 1, 1);
        glUseProgram(blit);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneTex);
        glUniform1i(glGetUniformLocation(blit, "t"), 0);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    std::vector<unsigned char> readBack() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        std::vector<unsigned char> px((size_t)W * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        return px;
    }
    bool loseContext() {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
        return ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, surf, surf, ctx);
    }
};

static double meanDiff(std::vector<unsigned char> const& a, std::vector<unsigned char> const& b) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); i += 4) for (int k = 0; k < 3; ++k) sum += std::abs((int)a[i + k] - (int)b[i + k]);
    return sum / ((double)(a.size() / 4) * 3.0);
}

static void setupPlayer() {
    static PlayerObject player; static cocos2d::CCNode parent; static PlayLayer pl;
    player.pos = {290.f * W / 1600.f, 160.f * H / 720.f}; player.parent = &parent; pl.m_player1 = &player; PlayLayer::slot() = &pl;
    cocos2d::CCDirector::sharedDirector()->win = {(float)W, (float)H};
}

static void defaults() {
    auto& c = dlss5::config();
    c.intensity = 1.f; c.detail = 1.f; c.bloom = 1.f; c.relight = 1.f; c.shadows = 1.f; c.film = 1.f;
    c.reflect = 1.f; c.gi = 1.f; c.look = 0; c.playerLight = true; c.menus = true; c.compare = false;
    c.enabled = true; c.autoQuality = false;
}

static int runRender(int argc, char** argv) {
    if (argc < 9) { std::puts("usage: preview render in.rgba out.rgba w h quality look frames [key=value...]"); return 1; }
    W = std::atoi(argv[4]); H = std::atoi(argv[5]);
    Gfx gfx; gfx.scene = readFile(argv[2], (size_t)W * H * 4);
    if (!gfx.init()) return 2;
    gfx.setupGL(); setupPlayer(); defaults();
    auto& c = dlss5::config();
    c.quality = std::atoi(argv[6]); c.look = std::atoi(argv[7]);
    int frames = std::atoi(argv[8]);
    for (int i = 9; i < argc; ++i) {
        std::string kv = argv[i]; auto eq = kv.find('='); if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq); float v = (float)std::atof(kv.c_str() + eq + 1);
        if (k == "intensity") c.intensity = v / 100.f; else if (k == "detail") c.detail = v / 100.f;
        else if (k == "bloom") c.bloom = v / 100.f; else if (k == "relight") c.relight = v / 100.f;
        else if (k == "shadows") c.shadows = v / 100.f; else if (k == "film") c.film = v / 100.f;
        else if (k == "reflect") c.reflect = v / 100.f; else if (k == "gi") c.gi = v / 100.f;
        else if (k == "light") c.playerLight = v > 0.5f;
        else if (k == "compare") { c.compare = v > 0.5f; c.comparePos = 0.5f; }
    }
    auto* dir = cocos2d::CCDirector::sharedDirector();
    for (int f = 0; f < frames; ++f) { gfx.drawGameFrame(); dir->frames++; dlss5::onFrameRendered(); }
    auto px = gfx.readBack();
    FILE* o = std::fopen(argv[3], "wb"); std::fwrite(px.data(), 1, px.size(), o); std::fclose(o);
    std::printf("rendered quality %s, %d frames, GL error=0x%x\n", dlss5::qualityName(c.quality), frames, glGetError());
    return 0;
}

static int runSelfTest(int argc, char** argv) {
    if (argc < 5) { std::puts("usage: preview selftest in.rgba w h"); return 1; }
    W = std::atoi(argv[3]); H = std::atoi(argv[4]);
    Gfx gfx; gfx.scene = readFile(argv[2], (size_t)W * H * 4);
    if (!gfx.init()) return 2;
    std::printf("GL_RENDERER=%s\n", glGetString(GL_RENDERER));
    gfx.setupGL(); setupPlayer(); defaults();
    auto& c = dlss5::config();
    auto* dir = cocos2d::CCDirector::sharedDirector();

    // runs `frames` game frames through the pipeline, each one starting from deliberately messy GL state
    auto runFrames = [&](int frames, bool checkState) {
        GLuint dummy[3]; glGenTextures(3, dummy);
        for (int f = 0; f < frames; ++f) {
            gfx.drawGameFrame();
            for (int i = 0; i < 3; ++i) { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, dummy[i]); }
            glActiveTexture(GL_TEXTURE1); glUseProgram(gfx.blit);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST); glEnable(GL_SCISSOR_TEST); glScissor(5, 5, 300, 200);
            glViewport(10, 10, 123, 45);
            glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
            Snapshot before = snap();
            dir->frames++;
            dlss5::onFrameRendered();
            bool noErr = glGetError() == GL_NO_ERROR;
            Snapshot after = snap();
            if (!noErr) { std::puts("  GL error left behind"); ++fails; }
            if (checkState && !(before == after)) { std::printf("  frame %d: GL state not restored\n", f); ++fails; }
            glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
        }
        glDeleteTextures(3, dummy);
    };

    for (int q = 0; q <= 3; ++q) {
        std::printf("--- quality %d (%s)\n", q, dlss5::qualityName(q));
        c.quality = q;
        runFrames(3, true);
        auto out = gfx.readBack();
        double d = meanDiff(out, gfx.scene);
        std::printf("  mean |processed - raw| = %.2f\n", d);
        CHECK(d > 3.0, "frame was not processed");
    }

    c.quality = 3;
    std::puts("--- compare mode (left half must equal the original)");
    c.compare = true; c.comparePos = 0.5f; runFrames(1, false);
    { auto out = gfx.readBack(); int bad = 0;
      for (int y = 0; y < H; ++y) for (int x = 0; x < W / 2 - 4; ++x) for (int k = 0; k < 3; ++k) if (out[((size_t)y * W + x) * 4 + k] != gfx.scene[((size_t)y * W + x) * 4 + k]) ++bad;
      CHECK(bad == 0, "left half differs from the original in compare mode"); }
    c.compare = false;

    std::puts("--- disabled: the frame must not be touched");
    c.enabled = false; gfx.drawGameFrame(); auto a = gfx.readBack(); dir->frames++; dlss5::onFrameRendered(); auto b = gfx.readBack();
    CHECK(a == b, "disabled mode modified the frame"); c.enabled = true;

    std::puts("--- simulated context loss (pause/resume) at MAX");
    CHECK(gfx.loseContext(), "could not recreate the EGL context");
    gfx.setupGL();
    runFrames(12, false);
    { double d = meanDiff(gfx.readBack(), gfx.scene); std::printf("  mean diff after recovery = %.2f\n", d); CHECK(d > 3.0, "not processing after context loss"); }

    std::puts("--- simulated stale handles with the same context pointer at MAX");
    for (GLuint id = 1; id < 96; ++id) { glDeleteFramebuffers(1, &id); glDeleteProgram(id); glDeleteBuffers(1, &id); }
    for (GLuint id = 1; id < 320; ++id) glDeleteTextures(1, &id);
    gfx.setupGL();
    int errs = 0; int lastErr = -1;
    for (int f = 0; f < 40; ++f) { gfx.drawGameFrame(); dir->frames++; dlss5::onFrameRendered(); if (glGetError() != GL_NO_ERROR) { ++errs; lastErr = f; } }
    std::printf("  GL errors visible to the app: %d (last at frame %d)\n", errs, lastErr);
    CHECK(lastErr < 20, "did not recover from stale handles");
    { double d = meanDiff(gfx.readBack(), gfx.scene); std::printf("  mean diff after recovery = %.2f\n", d); CHECK(d > 3.0, "not processing after stale handles"); }

    std::printf("RESULT: %s (%d failed checks)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "render")) return runRender(argc, argv);
    if (argc >= 2 && !std::strcmp(argv[1], "selftest")) return runSelfTest(argc, argv);
    std::puts("usage: preview render|selftest ...");
    return 1;
}

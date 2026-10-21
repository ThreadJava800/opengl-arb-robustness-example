/*
 * robust_triangle.c
 *
 * A minimal OpenGL program that:
 *   - creates a robust GL context (GL_ARB_robustness / GLFW_LOSE_CONTEXT_ON_RESET)
 *   - runs on Wayland (tested target: KDE KWin compositor) via GLFW's EGL backend
 *   - draws something every frame (a spinning RGB triangle)
 *   - logs every rendered frame
 *   - detects a GPU-initiated context reset and recovers by rebuilding
 *     the context and all GL state, then resumes rendering
 *
 * Press R to simulate the recovery path (tears down + rebuilds the context
 * without a real GPU hang, so you can see recovery work on demand).
 * Press ESC to quit.
 *
 * Build:  gcc robust_triangle.c -o robust_triangle -lglfw -lGL -lm
 * Run:    ./robust_triangle            (from a Wayland/KWin session)
 */

#define _POSIX_C_SOURCE 199309L

#include <GLFW/glfw3.h>   /* also pulls in <GL/gl.h> on Linux */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

/* --- GL_ARB_robustness enums, defined locally so we don't depend on glext.h --- */
#ifndef GL_GUILTY_CONTEXT_RESET_ARB
#define GL_GUILTY_CONTEXT_RESET_ARB    0x8253
#define GL_INNOCENT_CONTEXT_RESET_ARB  0x8254
#define GL_UNKNOWN_CONTEXT_RESET_ARB   0x8255
#endif

/* glGetGraphicsResetStatusARB() -> GLenum */
typedef GLenum (APIENTRY *PFN_glGetGraphicsResetStatus)(void);

typedef struct {
    GLFWwindow                   *win;
    PFN_glGetGraphicsResetStatus  getResetStatus; /* NULL if unavailable */
    int                           force_reset;     /* set by the R key    */
} App;

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static const char *reset_status_name(GLenum s)
{
    switch (s) {
        case GL_NO_ERROR:                    return "GL_NO_ERROR";
        case GL_GUILTY_CONTEXT_RESET_ARB:    return "GL_GUILTY_CONTEXT_RESET_ARB";
        case GL_INNOCENT_CONTEXT_RESET_ARB:  return "GL_INNOCENT_CONTEXT_RESET_ARB";
        case GL_UNKNOWN_CONTEXT_RESET_ARB:   return "GL_UNKNOWN_CONTEXT_RESET_ARB";
        default:                             return "unexpected status";
    }
}

static void key_cb(GLFWwindow *w, int key, int sc, int action, int mods)
{
    (void)sc; (void)mods;
    if (action != GLFW_PRESS) return;
    App *app = (App *)glfwGetWindowUserPointer(w);
    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(w, GLFW_TRUE);
    } else if (key == GLFW_KEY_R) {
        app->force_reset = 1;
        printf("[input] simulated reset requested (R)\n");
    }
}

/* Create the window + robust GL context and (re)load all GL-side state. */
static int create_context(App *app)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);   /* legacy immediate mode */
    /* THE key hint: ask the driver for a robust context that loses itself on
     * reset and lets us query glGetGraphicsResetStatus. GLFW maps this to the
     * EGL_EXT_create_context_robustness attributes on the Wayland/EGL path. */
    glfwWindowHint(GLFW_CONTEXT_ROBUSTNESS,    GLFW_LOSE_CONTEXT_ON_RESET);
    glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);

    app->win = glfwCreateWindow(640, 480, "ARB_robustness demo (Wayland/KWin)",
                                NULL, NULL);
    if (!app->win) {
        fprintf(stderr, "[gl] failed to create window/context\n");
        return 0;
    }

    glfwSetWindowUserPointer(app->win, app);
    glfwSetKeyCallback(app->win, key_cb);
    glfwMakeContextCurrent(app->win);
    glfwSwapInterval(1);

    /* Load the reset-status entry point. Try the ARB name first, then the
     * core (GL 4.5) / KHR spellings as fallbacks. */
    app->getResetStatus =
        (PFN_glGetGraphicsResetStatus)glfwGetProcAddress("glGetGraphicsResetStatusARB");
    if (!app->getResetStatus)
        app->getResetStatus =
            (PFN_glGetGraphicsResetStatus)glfwGetProcAddress("glGetGraphicsResetStatus");
    if (!app->getResetStatus)
        app->getResetStatus =
            (PFN_glGetGraphicsResetStatus)glfwGetProcAddress("glGetGraphicsResetStatusKHR");

    const char *vendor   = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version  = (const char *)glGetString(GL_VERSION);
    const char *exts     = (const char *)glGetString(GL_EXTENSIONS);
    int have_arb = (exts && strstr(exts, "GL_ARB_robustness")) ? 1 : 0;

    printf("[gl] vendor=%s | renderer=%s | version=%s\n",
           vendor ? vendor : "?", renderer ? renderer : "?",
           version ? version : "?");
    printf("[gl] GL_ARB_robustness advertised : %s\n", have_arb ? "yes" : "no");
    printf("[gl] reset-status query loaded     : %s\n",
           app->getResetStatus ? "yes" : "no (resets won't be detectable)");

    /* (Re)initialise any GL state here. After a real reset the previous
     * context is gone, so everything created against it must be remade. This
     * demo only uses immediate mode, so there's nothing persistent to rebuild
     * beyond clear colour, which we set per-frame in render(). */

    app->force_reset = 0;
    return 1;
}

static void destroy_context(App *app)
{
    if (app->win) {
        glfwDestroyWindow(app->win);
        app->win = NULL;
    }
    app->getResetStatus = NULL;
}

static void render(App *app, double t)
{
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(app->win, &fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) return;
    glViewport(0, 0, fbw, fbh);

    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float aspect = (float)fbh / (float)fbw;   /* keep the triangle from stretching */
    float r      = 0.6f;
    float colors[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };

    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 3; i++) {
        float ang = (float)t + (float)i * (2.0f * 3.14159265f / 3.0f);
        float x   = r * cosf(ang) * aspect;
        float y   = r * sinf(ang);
        glColor3f(colors[i][0], colors[i][1], colors[i][2]);
        glVertex2f(x, y);
    }
    glEnd();
}

int main(void)
{
    /* Prefer Wayland when we're in a Wayland session and the GLFW build
     * supports it; fall back to auto-detect otherwise. The guards keep this
     * compiling against GLFW 3.3 (which lacks the platform-selection API). */
#ifdef GLFW_PLATFORM_WAYLAND
    if (getenv("WAYLAND_DISPLAY"))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif
    if (!glfwInit()) {
#ifdef GLFW_PLATFORM_WAYLAND
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);  /* retry, auto-detect */
        if (!glfwInit())
#endif
        {
            fprintf(stderr, "glfwInit failed\n");
            return 1;
        }
    }

#ifdef GLFW_PLATFORM_WAYLAND
    {
        int p = glfwGetPlatform();
        printf("[glfw] platform: %s\n",
               p == GLFW_PLATFORM_WAYLAND ? "Wayland" :
               p == GLFW_PLATFORM_X11     ? "X11"     : "other");
    }
#endif

    App app = {0};
    if (!create_context(&app)) {
        glfwTerminate();
        return 1;
    }

    uint64_t frame = 0;
    double   t0    = now_seconds();

    while (!glfwWindowShouldClose(app.win)) {
        double t = now_seconds() - t0;

        /* 1) Check for a GPU-initiated reset *before* touching the context. */
        GLenum status = GL_NO_ERROR;
        if (app.getResetStatus)
            status = app.getResetStatus();

        if (status != GL_NO_ERROR || app.force_reset) {
            if (app.force_reset)
                printf("[reset] simulated reset -> rebuilding context\n");
            else
                printf("[reset] GPU reset detected: %s -> rebuilding context\n",
                       reset_status_name(status));

            destroy_context(&app);

            /* Rebuild, with a short backoff + bounded retries in case the
             * GPU is still coming back after the reset. */
            int ok = 0;
            for (int attempt = 1; attempt <= 10 && !ok; attempt++) {
                struct timespec backoff = {0, 200L * 1000L * 1000L}; /* 200 ms */
                nanosleep(&backoff, NULL);
                ok = create_context(&app);
                if (!ok)
                    fprintf(stderr, "[reset] rebuild attempt %d failed\n", attempt);
            }
            if (!ok) {
                fprintf(stderr, "[reset] could not recover, giving up\n");
                break;
            }
            printf("[reset] recovery complete, resuming rendering\n");
            continue; /* re-enter loop with the fresh, valid context */
        }

        /* 2) Render + log every frame. */
        printf("[frame] t=%.3f  rendering frame %llu\n",
               t, (unsigned long long)frame);
        fflush(stdout);

        render(&app, t);

        glfwSwapBuffers(app.win);
        glfwPollEvents();
        frame++;
    }

    destroy_context(&app);
    glfwTerminate();
    return 0;
}


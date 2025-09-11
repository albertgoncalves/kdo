#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GL_GLEXT_PROTOTYPES

#include <GLFW/glfw3.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  i32;
typedef float    f32;
typedef double   f64;

typedef struct stat     FileStat;
typedef struct timespec Time;

typedef struct {
    void* address;
    u32   len;
} MemMap;

typedef enum {
    FALSE = 0,
    TRUE,
} Bool;

typedef struct {
    Bool x, y;
} Vec2b;

typedef struct {
    f32 x, y;
} Vec2f;

typedef struct {
    Vec2f center, scale;
} Rect;

#define CAP_BUFFER (1 << 12)
static char BUFFER[CAP_BUFFER];
static u32  LEN_BUFFER = 0;

#define WINDOW_X    1200
#define WINDOW_Y    1000
#define WINDOW_NAME "kdo"

#define BACKGROUND_COLOR        0.125f, 0.125f, 0.125f, 1.0f
#define BACKGROUND_COLOR_PAUSED (1.0f - 0.125f), (1.0f - 0.125f), (1.0f - 0.125f), 1.0f

#define INDEX_VERTEX    0
#define INDEX_TRANSLATE 1
#define INDEX_SCALE     2

#define MILLI_PER_SECOND 1000llu
#define NANO_PER_SECOND  1000000000llu
#define NANO_PER_MILLI   (NANO_PER_SECOND / MILLI_PER_SECOND)

// NOTE: See `https://docs.nvidia.com/drive/drive_os_5.1.6.1L/nvvib_docs/DRIVE_OS_Linux_SDK_Development_Guide/Graphics/graphics_opengl.html`.
static const Vec2f VERTICES[] = {
    {0.5f, 0.5f},
    {0.5f, -0.5f},
    {-0.5f, 0.5f},
    {-0.5f, -0.5f},
};

#define PATH_SHADER_VERT "./src/vert.glsl"
#define PATH_SHADER_FRAG "./src/frag.glsl"

#define FRAME_UPDATE_COUNT 9
#define FRAME_DURATION     ((u64)((1.0 / 60.0) * NANO_PER_SECOND))
#define FRAME_UPDATE_STEP  (FRAME_DURATION / FRAME_UPDATE_COUNT)

#define CAMERA_INIT    ((Vec2f){-250.0f, -500.0f})
#define CAMERA_OFFSET  ((Vec2f){0.0f, 100.0f})
#define CAMERA_LATENCY ((Vec2f){400.0f, 200.0f})

static Vec2f CAMERA;

#define RUN      0.03575f
#define LEAP     2.0f
#define FRICTION 0.9675f
#define DRAG     0.95f
#define JUMP     1.9f
#define GRAVITY  0.01f
#define DROP     7.0f
#define STICK    0.785f
#define GRAB     0.9f
#define BOUNCE   0.5325f
#define RESET    -900.0f

#define PLAYER_CENTER_INIT ((Vec2f){0.0f, 500.0f})
#define PLAYER_SCALE       ((Vec2f){27.5f, 27.5f})

static Rect RECTS[] = {
    {{0.0f, 0.0f}, PLAYER_SCALE},
    {{-15.f, 30.0f}, {900.0f, 5.0f}},
    {{0.0f, 175.0f}, {250.0f, 5.0f}},
    {{-650.0f, 100.0f}, {250.0f, 5.0f}},
    {{625.0f, -400.0f}, {5.0f, 600.0f}},
    {{500.0f, 0.0f}, {5.0f, 500.0f}},
    {{700.0f, 350.0f}, {5.0f, 300.0f}},
    {{600.0f, 1000.0f}, {5.0f, 900.0f}},
    {{1000.0f, 400.0f}, {250.0f, 5.0f}},
    {{800.0f, 1200.0f}, {5.0f, 125.0f}},
    {{900.0f, 900.0f}, {50.0f, 5.0f}},
    {{-300.0f, 500.0f}, {5.0f, 250.0f}},
    {{-1350.0f, 450.0f}, {800.0f, 5.0f}},
};
#define LEN_RECTS (sizeof(RECTS) / sizeof(RECTS[0]))

#define PLAYER RECTS[0]

static Vec2f PLAYER_SPEED = {0};
static Bool  PLAYER_CAN_JUMP;
static Bool  PLAYER_CAN_LEAP;

static Bool PAUSED = FALSE;

static i32 UNIFORM_PAUSED;

#define EXIT_IF_GL_ERROR()                                   \
    {                                                        \
        switch (glGetError()) {                              \
        case GL_INVALID_ENUM: {                              \
            assert(0 && "GL_INVALID_ENUM");                  \
        }                                                    \
        case GL_INVALID_VALUE: {                             \
            assert(0 && "GL_INVALID_VALUE");                 \
        }                                                    \
        case GL_INVALID_OPERATION: {                         \
            assert(0 && "GL_INVALID_OPERATION");             \
        }                                                    \
        case GL_INVALID_FRAMEBUFFER_OPERATION: {             \
            assert(0 && "GL_INVALID_FRAMEBUFFER_OPERATION"); \
        }                                                    \
        case GL_OUT_OF_MEMORY: {                             \
            assert(0 && "GL_OUT_OF_MEMORY");                 \
        }                                                    \
        case GL_NO_ERROR: {                                  \
            break;                                           \
        }                                                    \
        default: {                                           \
            assert(0);                                       \
        }                                                    \
        }                                                    \
    }

static u64 now(void) {
    Time time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return ((u64)time.tv_sec * 1000000000llu) + (u64)time.tv_nsec;
}

static f32 lerp_f32(f32 l, f32 r, f32 t) {
    return l + (t * (r - l));
}

static Vec2f lerp_vec2f(Vec2f l, Vec2f r, f32 t) {
    return (Vec2f){
        .x = lerp_f32(l.x, r.x, t),
        .y = lerp_f32(l.y, r.y, t),
    };
}

__attribute__((noreturn)) static void callback_error(i32 code, const char* error) {
    printf("%d: %s\n", code, error);
    assert(0);
}

static void compile_shader(const char* path, u32 shader) {
    const i32 file = open(path, O_RDONLY);
    assert(0 <= file);

    FileStat stat;
    assert(0 <= fstat(file, &stat));

    const MemMap map = {
        .address = mmap(NULL, (u32)stat.st_size, PROT_READ, MAP_SHARED, file, 0),
        .len     = (u32)stat.st_size,
    };
    assert(map.address != MAP_FAILED);
    close(file);

    const u32 prev = LEN_BUFFER;

    assert((LEN_BUFFER + map.len + 1) < CAP_BUFFER);
    char* source = &BUFFER[LEN_BUFFER];
    memcpy(source, (const char*)map.address, map.len);

    LEN_BUFFER += map.len;
    BUFFER[LEN_BUFFER++] = '\0';

    glShaderSource(shader, 1, (const char* const*)(&source), NULL);
    glCompileShader(shader);

    i32 status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        glGetShaderInfoLog(shader, (i32)(CAP_BUFFER - LEN_BUFFER), NULL, &BUFFER[LEN_BUFFER]);
        printf("%s", &BUFFER[LEN_BUFFER]);
    }

    assert(status);

    assert(!munmap(map.address, map.len));
    LEN_BUFFER = prev;
}

#define BIND_BUFFER(object, array, target, usage)          \
    {                                                      \
        glGenBuffers(1, &object);                          \
        glBindBuffer(target, object);                      \
        glBufferData(target, sizeof(array), array, usage); \
        EXIT_IF_GL_ERROR();                                \
    }

static void callback_key(GLFWwindow* window, i32 key, i32, i32 action, i32) {
    if (action != GLFW_RELEASE) {
        return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE: {
        glfwSetWindowShouldClose(window, TRUE);
        break;
    }
    case GLFW_KEY_E: {
        PAUSED = !PAUSED;
        glUniform1ui(UNIFORM_PAUSED, PAUSED);
        if (PAUSED) {
            glClearColor(BACKGROUND_COLOR_PAUSED);
        } else {
            glClearColor(BACKGROUND_COLOR);
        }
        break;
    }
    default: {
    }
    }
}

static Bool intersect(Rect a, Rect b) {
    const Vec2f a_scale_half = {
        .x = a.scale.x / 2.0f,
        .y = a.scale.y / 2.0f,
    };
    const Vec2f b_scale_half = {
        .x = b.scale.x / 2.0f,
        .y = b.scale.y / 2.0f,
    };
    const Vec2f a_left_bottom = {
        .x = a.center.x - a_scale_half.x,
        .y = a.center.y - a_scale_half.y,
    };
    const Vec2f b_left_bottom = {
        .x = b.center.x - b_scale_half.x,
        .y = b.center.y - b_scale_half.y,
    };
    const Vec2f a_right_top = {
        .x = a.center.x + a_scale_half.x,
        .y = a.center.y + a_scale_half.y,
    };
    const Vec2f b_right_top = {
        .x = b.center.x + b_scale_half.x,
        .y = b.center.y + b_scale_half.y,
    };
    return ((a_left_bottom.x < b_right_top.x) && (b_left_bottom.x < a_right_top.x) &&
            (a_left_bottom.y < b_right_top.y) && (b_left_bottom.y < a_right_top.y));
}

static Vec2b find_collisions(void) {
    // NOTE: See `https://www.gamedev.net/articles/programming/general-and-gameplay-programming/swept-aabb-collision-detection-and-response-r3084/`.
    Rect left_right = {
        .center = {.y = PLAYER.center.y},
        .scale  = {.y = PLAYER.scale.y},
    };
    if (PLAYER_SPEED.x < 0.0f) {
        left_right.center.x = PLAYER.center.x + ((-PLAYER.scale.x + PLAYER_SPEED.x) / 2.0f);
        left_right.scale.x  = -PLAYER_SPEED.x;
    } else {
        left_right.center.x = PLAYER.center.x + ((PLAYER.scale.x + PLAYER_SPEED.x) / 2.0f);
        left_right.scale.x  = PLAYER_SPEED.x;
    }
    Rect bottom_top = {
        .center = {.x = PLAYER.center.x},
        .scale  = {.x = PLAYER.scale.x},
    };
    if (PLAYER_SPEED.y < 0.0f) {
        bottom_top.center.y = PLAYER.center.y + ((-PLAYER.scale.y + PLAYER_SPEED.y) / 2.0f);
        bottom_top.scale.y  = -PLAYER_SPEED.y;
    } else {
        bottom_top.center.y = PLAYER.center.y + ((PLAYER.scale.y + PLAYER_SPEED.y) / 2.0f);
        bottom_top.scale.y  = PLAYER_SPEED.y;
    }

    Vec2b collision = {0};

    for (u32 i = 1; i < LEN_RECTS; ++i) {
        collision.x |= intersect(left_right, RECTS[i]);
        collision.y |= intersect(bottom_top, RECTS[i]);
        if (collision.x && collision.y) {
            break;
        }
    }

    return collision;
}

static void step(GLFWwindow* window, f32 t) {
    assert(t <= 1.0f);

    {
        const Vec2f prev = CAMERA;
        CAMERA.x -= ((CAMERA.x - CAMERA_OFFSET.x) - PLAYER.center.x) / CAMERA_LATENCY.x;
        CAMERA.y -= ((CAMERA.y - CAMERA_OFFSET.y) - PLAYER.center.y) / CAMERA_LATENCY.y;
        CAMERA = lerp_vec2f(prev, CAMERA, t);
    }

    if (PAUSED) {
        return;
    }

    {
        const Vec2f prev = PLAYER_SPEED;

        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            PLAYER_SPEED.x += RUN;
        }

        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            PLAYER_SPEED.x -= RUN;
        }

        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            PLAYER_SPEED.y -= GRAVITY * DROP;
        } else {
            PLAYER_SPEED.y -= GRAVITY;
        }

        PLAYER_SPEED = lerp_vec2f(prev, PLAYER_SPEED, t);
    }

    if (PLAYER_CAN_JUMP) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            PLAYER_SPEED.y += JUMP;
            PLAYER_CAN_JUMP = FALSE;

            if (PLAYER_CAN_LEAP) {
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                    PLAYER_SPEED.x -= LEAP;
                }
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                    PLAYER_SPEED.x += LEAP;
                }
                PLAYER_CAN_LEAP = FALSE;
            }
        }
    }

    const Vec2b collision = find_collisions();

    {
        const Vec2f prev = PLAYER_SPEED;

        if (collision.x) {
            PLAYER_SPEED.x = -PLAYER_SPEED.x * BOUNCE;
            PLAYER_SPEED.y *= GRAB;
        }

        if (collision.y) {
            PLAYER_SPEED.x *= FRICTION;
            PLAYER_SPEED.y = -PLAYER_SPEED.y * BOUNCE;
        } else {
            PLAYER_SPEED.x *= DRAG;
        }

        PLAYER_SPEED = lerp_vec2f(prev, PLAYER_SPEED, t);
    }

    if (!collision.x) {
        PLAYER.center.x += PLAYER_SPEED.x;
    }

    if (collision.y) {
        if (PLAYER_SPEED.y < STICK) {
            PLAYER_SPEED.y = 0.0f;
        }
    } else {
        PLAYER.center.y += PLAYER_SPEED.y;
    }

    if (PLAYER.center.y < RESET) {
        PLAYER.center = PLAYER_CENTER_INIT;
        PLAYER_SPEED  = (Vec2f){0};
    }

    PLAYER_CAN_JUMP = (collision.x || collision.y) && (PLAYER_SPEED.y <= 0.0f);
    PLAYER_CAN_LEAP = (!collision.y) && collision.x;
}

i32 main(void) {
    printf("GLFW version : %s\n", glfwGetVersionString());

    assert(glfwInit());
    glfwSetErrorCallback(callback_error);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, FALSE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_X, WINDOW_Y, WINDOW_NAME, NULL, NULL);
    assert(window);

    glfwSetKeyCallback(window, callback_key);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glClearColor(BACKGROUND_COLOR);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    EXIT_IF_GL_ERROR()

    u32 vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    EXIT_IF_GL_ERROR();

    u32 vbo;
    BIND_BUFFER(vbo, VERTICES, GL_ARRAY_BUFFER, GL_STATIC_DRAW);

    glEnableVertexAttribArray(INDEX_VERTEX);
    glVertexAttribPointer(INDEX_VERTEX, 2, GL_FLOAT, FALSE, sizeof(VERTICES[0]), 0);
    EXIT_IF_GL_ERROR();

    u32 instance_vbo;
    BIND_BUFFER(instance_vbo, RECTS, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(INDEX_TRANSLATE);
    glVertexAttribPointer(INDEX_TRANSLATE,
                          2,
                          GL_FLOAT,
                          FALSE,
                          sizeof(RECTS[0]),
                          (void*)offsetof(Rect, center));
    glVertexAttribDivisor(INDEX_TRANSLATE, 1);
    EXIT_IF_GL_ERROR();

    glEnableVertexAttribArray(INDEX_SCALE);
    glVertexAttribPointer(INDEX_SCALE,
                          2,
                          GL_FLOAT,
                          FALSE,
                          sizeof(RECTS[0]),
                          (void*)offsetof(Rect, scale));
    glVertexAttribDivisor(INDEX_SCALE, 1);
    EXIT_IF_GL_ERROR();

    glBufferSubData(GL_ARRAY_BUFFER,
                    sizeof(RECTS[0]),
                    (LEN_RECTS - 1) * sizeof(RECTS[0]),
                    &RECTS[1]);
    EXIT_IF_GL_ERROR()

    PLAYER.center = PLAYER_CENTER_INIT;

    CAMERA.x = CAMERA_INIT.x + CAMERA_OFFSET.x;
    CAMERA.y = CAMERA_INIT.y + CAMERA_OFFSET.y;

    glViewport(0, 0, WINDOW_X, WINDOW_Y);

    const u32 program = glCreateProgram();

    const u32 shader_vert = glCreateShader(GL_VERTEX_SHADER);
    const u32 shader_frag = glCreateShader(GL_FRAGMENT_SHADER);

    compile_shader(PATH_SHADER_VERT, shader_vert);
    compile_shader(PATH_SHADER_FRAG, shader_frag);

    assert(LEN_BUFFER == 0);

    glAttachShader(program, shader_vert);
    glAttachShader(program, shader_frag);
    glLinkProgram(program);

    {
        i32 status = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &status);

        if (!status) {
            glGetProgramInfoLog(program, CAP_BUFFER, NULL, &BUFFER[0]);
            printf("%s", &BUFFER[0]);
            assert(0);
        }
    }

    glDeleteShader(shader_vert);
    glDeleteShader(shader_frag);
    glUseProgram(program);

    const i32 uniform_camera = glGetUniformLocation(program, "camera");
    const i32 uniform_window = glGetUniformLocation(program, "window");
    const i32 uniform_time   = glGetUniformLocation(program, "time");
    UNIFORM_PAUSED           = glGetUniformLocation(program, "paused");

    glUniform2f(uniform_window, WINDOW_X, WINDOW_Y);
    glUniform1ui(UNIFORM_PAUSED, PAUSED);
    EXIT_IF_GL_ERROR();

    u64 prev    = now();
    u64 elapsed = 0;
    u64 frames  = 0;

    printf("\n\n");
    while (!glfwWindowShouldClose(window)) {
        const u64 start = now();

        u64 delta = start - prev;
        elapsed += delta;
        prev = start;

        if (NANO_PER_SECOND <= elapsed) {
            printf("\033[2A"
                   "%7.4f ms/f\n"
                   "%7lu f/s\n",
                   (NANO_PER_SECOND / (f64)frames) / NANO_PER_MILLI,
                   frames);
            elapsed = 0;
            frames  = 0;
        }

        ++frames;

        glfwPollEvents();

        for (; FRAME_UPDATE_STEP < delta; delta -= FRAME_UPDATE_STEP) {
            step(window, 1.0f);
        }
        step(window, ((f32)delta) / FRAME_UPDATE_STEP);

        glUniform2f(uniform_camera, CAMERA.x, CAMERA.y);
        // NOTE: See `http://the-witness.net/news/2022/02/a-shader-trick/`.
        glUniform1f(uniform_time,
                    ((f32)(now() % (NANO_PER_SECOND * 10llu))) / ((f32)NANO_PER_SECOND));

        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(RECTS[0]), RECTS);

        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 6, (i32)LEN_RECTS);

        EXIT_IF_GL_ERROR()

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &instance_vbo);
    glDeleteProgram(program);

    glfwTerminate();

    return 0;
}

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GL_GLEXT_PROTOTYPES

#include <GLFW/glfw3.h>

typedef uint8_t  u8;
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
    f32 x, y;
} Vec2f;

typedef struct {
    Vec2f center, scale;
} Rect;

typedef struct {
    Vec2f left_bottom, right_top;
} Box;

typedef enum {
    HIT_NONE = 0,
    HIT_X    = 1 << 0,
    HIT_Y    = 1 << 1,
} Hit;

typedef struct {
    f32 time, overlap;
    Hit hit;
} Collision;

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

#define NANO_PER_SECOND 1000000000

// NOTE: See `https://docs.nvidia.com/drive/drive_os_5.1.6.1L/nvvib_docs/DRIVE_OS_Linux_SDK_Development_Guide/Graphics/graphics_opengl.html`.
static const Vec2f VERTICES[] = {{0.5f, 0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {-0.5f, -0.5f}};

#define PATH_SHADER_VERT "./src/vert.glsl"
#define PATH_SHADER_FRAG "./src/frag.glsl"

#define FRAME_UPDATE_COUNT 9
#define FRAME_DURATION     ((1.0 / 60.0) * NANO_PER_SECOND)
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
    {{500.0f, 50.0f}, {5.0f, 500.0f}},
    {{700.0f, 300.0f}, {5.0f, 300.0f}},
    {{600.0f, 1000.0f}, {5.0f, 900.0f}},
    {{1000.0f, 400.0f}, {250.0f, 5.0f}},
    {{800.0f, 1200.0f}, {5.0f, 125.0f}},
    {{900.0f, 900.0f}, {50.0f, 5.0f}},
    {{-300.0f, 500.0f}, {5.0f, 250.0f}},
    {{-1350.0f, 450.0f}, {800.0f, 5.0f}},
};
#define LEN_RECTS (sizeof(RECTS) / sizeof(RECTS[0]))

static Box BOXES[LEN_RECTS - 1];

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

static u64 clock_monotonic(void) {
    Time time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return (((u64)time.tv_sec) * 1000000000llu) + (u64)time.tv_nsec;
}

static f32 lerp_f32(f32 l, f32 r, f32 t) {
    return l + (t * (r - l));
}

static Vec2f lerp_vec2f(Vec2f l, Vec2f r, f32 t) {
    return (Vec2f){.x = lerp_f32(l.x, r.x, t), .y = lerp_f32(l.y, r.y, t)};
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

static Box rect_to_box(Rect rect) {
    const Vec2f half_scale = (Vec2f){.x = rect.scale.x / 2.0f, .y = rect.scale.y / 2.0f};
    return (Box){
        .left_bottom.x = rect.center.x - half_scale.x,
        .left_bottom.y = rect.center.y - half_scale.y,
        .right_top.x   = rect.center.x + half_scale.x,
        .right_top.y   = rect.center.y + half_scale.y,
    };
}

static Box move(const Box* box, Vec2f speed, f32 t) {
    const Vec2f distance = (Vec2f){.x = speed.x * t, .y = speed.y * t};
    return (Box){
        .left_bottom = {.x = box->left_bottom.x + distance.x, .y = box->left_bottom.y + distance.y},
        .right_top   = {.x = box->right_top.x + distance.x, .y = box->right_top.y + distance.y},
    };
}

static f32 min(f32 a, f32 b) {
    return a < b ? a : b;
}

static f32 max(f32 a, f32 b) {
    return a < b ? b : a;
}

static f32 overlap_segment(f32 l0, f32 r0, f32 l1, f32 r1) {
    return max(0.0f, min(r0, r1) - max(l0, l1));
}

static f32 overlap_box(const Box* l, const Box* r) {
    return overlap_segment(l->left_bottom.x, l->right_top.x, r->left_bottom.x, r->right_top.x) +
           overlap_segment(l->left_bottom.y, l->right_top.y, r->left_bottom.y, r->right_top.y);
}

static Collision find_collision(const Box* from, const Box* obstacle, Vec2f speed) {
    Vec2f time = {-INFINITY, -INFINITY};

    if (0.0f < speed.x) {
        time.x = (obstacle->left_bottom.x - from->right_top.x) / speed.x;
    } else if (speed.x < 0.0f) {
        time.x = (obstacle->right_top.x - from->left_bottom.x) / speed.x;
    }

    if (0.0f < speed.y) {
        time.y = (obstacle->left_bottom.y - from->right_top.y) / speed.y;
    } else if (speed.y < 0.0f) {
        time.y = (obstacle->right_top.y - from->left_bottom.y) / speed.y;
    }

    Collision collision = {0};

    if (time.y < time.x) {
        if ((time.x < 0.0f) || (1.0f < time.x)) {
            return collision;
        }

        Box to = move(from, speed, time.x);
        if ((to.left_bottom.y < obstacle->right_top.y) &&
            (obstacle->left_bottom.y < to.right_top.y))
        {
            collision.time    = time.x;
            collision.overlap = overlap_box(&to, obstacle);
            collision.hit     = HIT_X;
        }
    } else {
        if ((time.y < 0.0f) || (1.0f < time.y)) {
            return collision;
        }

        Box to = move(from, speed, time.y);
        if ((to.left_bottom.x < obstacle->right_top.x) &&
            (obstacle->left_bottom.x < to.right_top.x))
        {
            collision.time    = time.y;
            collision.overlap = overlap_box(&to, obstacle);
            collision.hit     = HIT_Y;
        }
    }

    return collision;
}

// TODO: Need a better name for this function.
static u8 find_all_collisions(void) {
    // NOTE: See `https://www.gamedev.net/articles/programming/general-and-gameplay-programming/swept-aabb-collision-detection-and-response-r3084/`.
    Vec2f speed     = PLAYER_SPEED;
    Vec2f remaining = PLAYER_SPEED;

    u8 hits = 0;

    for (u32 _ = 0; _ < 2; ++_) {
        const Box player_box = rect_to_box(PLAYER);
        Collision collision  = {0};

        for (u32 i = 0; i < (LEN_RECTS - 1); ++i) {
            // NOTE: This *should* work because `BOXES` is sorted by `x`.
            if (BOXES[i].right_top.x < (player_box.left_bottom.x + PLAYER_SPEED.x)) {
                continue;
            }
            if ((player_box.right_top.x + PLAYER_SPEED.x) < BOXES[i].left_bottom.x) {
                break;
            }

            const Collision candidate = find_collision(&player_box, &BOXES[i], speed);

            if (!candidate.hit) {
                continue;
            }
            if (!collision.hit) {
                collision = candidate;
                continue;
            }
            if (candidate.time < collision.time) {
                collision = candidate;
                continue;
            }
            if (((*(const u32*)&candidate.time) == (*(const u32*)&collision.time)) &&
                (collision.overlap < candidate.overlap))
            {
                collision = candidate;
            }
        }

        if (!collision.hit) {
            break;
        }

        speed.x *= collision.time;
        speed.y *= collision.time;
        PLAYER.center.x += speed.x;
        PLAYER.center.y += speed.y;
        remaining.x -= speed.x;
        remaining.y -= speed.y;

        switch (collision.hit) {
        case HIT_X: {
            remaining.x = 0.0f;
            break;
        }
        case HIT_Y: {
            remaining.y = 0.0f;
            break;
        }
        case HIT_NONE:
        default: {
            assert(0);
        }
        }

        speed.x = remaining.x;
        speed.y = remaining.y;
        hits |= collision.hit;
    }

    return hits;
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

    const u8 hits = find_all_collisions();

    // TODO: Do we need to scale `PLAYER_SPEED` by `t` here?
    if (hits & HIT_X) {
        PLAYER_SPEED.x = -PLAYER_SPEED.x * BOUNCE;
        PLAYER_SPEED.y *= GRAB;
    }

    // TODO: Do we need to scale `PLAYER_SPEED` by `t` here?
    if (hits & HIT_Y) {
        PLAYER_SPEED.x *= FRICTION;
        PLAYER_SPEED.y = -PLAYER_SPEED.y * BOUNCE;
    } else {
        PLAYER_SPEED.x *= DRAG;
    }

    if (!(hits & HIT_X)) {
        PLAYER.center.x += PLAYER_SPEED.x;
    }

    if (!(hits & HIT_Y)) {
        PLAYER.center.y += PLAYER_SPEED.y;
    } else if (PLAYER_SPEED.y < STICK) {
        PLAYER_SPEED.y = 0.0f;
    }

    if (PLAYER.center.y < RESET) {
        PLAYER.center = PLAYER_CENTER_INIT;
        PLAYER_SPEED  = (Vec2f){0};
    }

    PLAYER_CAN_JUMP = (hits != 0) && (PLAYER_SPEED.y <= 0.0f);
    PLAYER_CAN_LEAP = hits == HIT_X;
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

    for (u32 i = 0; i < (LEN_RECTS - 1); ++i) {
        BOXES[i] = rect_to_box(RECTS[i + 1]);
    }

    for (u32 i = 0; i < (LEN_RECTS - 1); ++i) {
        const Box box = BOXES[i];

        u32 j = i;
        for (; (0 < j) && (box.left_bottom.x < BOXES[j - 1].left_bottom.x); --j) {
            BOXES[j] = BOXES[j - 1];
        }
        BOXES[j] = box;
    }

    u64 prev        = clock_monotonic();
    u64 nanoseconds = 0;
    u64 frames      = 0;
    u64 steps       = 0;

    printf("\n\n\n\n\n");
    while (!glfwWindowShouldClose(window)) {
        const u64 now     = clock_monotonic();
        const u64 elapsed = now - prev;

        nanoseconds += elapsed;
        prev = now;

        if (NANO_PER_SECOND <= nanoseconds) {
            printf("\033[5A"
                   "%15lu nanoseconds\n"
                   "%15lu frames\n"
                   "%15lu steps\n"
                   "%15.4f nanoseconds/frame\n"
                   "%15.4f steps/frame\n",
                   nanoseconds,
                   frames,
                   steps,
                   ((f64)nanoseconds) / ((f64)frames),
                   ((f64)steps) / ((f64)frames));

            nanoseconds = 0;
            frames      = 0;
            steps       = 0;
        }

        ++frames;

        glfwPollEvents();

        f64 delta = (f64)elapsed;
        for (; FRAME_UPDATE_STEP < delta; delta -= FRAME_UPDATE_STEP) {
            step(window, 1.0f);
            ++steps;
        }
        step(window, (f32)(delta / FRAME_UPDATE_STEP));

        glUniform2f(uniform_camera, CAMERA.x, CAMERA.y);
        // NOTE: See `http://the-witness.net/news/2022/02/a-shader-trick/`.
        glUniform1f(uniform_time,
                    ((f32)(clock_monotonic() % (NANO_PER_SECOND * 10llu))) / NANO_PER_SECOND);

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

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
    const char* buffer;
    u32         len;
} String;

typedef struct {
    f32 x, y;
} Vec2f;

typedef struct {
    u8 x, y, z;
} Vec3u;

typedef struct {
    Vec2f center, scale;
} Rect;

#define STRING(literal)      \
    ((String){               \
        literal,             \
        sizeof(literal) - 1, \
    })

#define CAP_BUFFER (1 << 12)
static char BUFFER[CAP_BUFFER];
static u32  LEN_BUFFER = 0;

#if 1
    #define WINDOW_X 1200
    #define WINDOW_Y 1000
#else
    #define WINDOW_X 900
    #define WINDOW_Y 900
#endif
#define WINDOW_NAME "kdo"

#define PREFIX "\033[3A\n\n  # "

#define BACKGROUND_COLOR 0.125f, 0.125f, 0.125f, 1.0f
#define BACKGROUND_COLOR_PAUSED \
    (1.0f - 0.125f), (1.0f - 0.125f), (1.0f - 0.125f), 1.0f

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

static const char* PATH_CONFIG;
static const char* PATH_SHADER_VERT;
static const char* PATH_SHADER_FRAG;

static u64 FRAME_UPDATE_COUNT;
#define FRAME_DURATION    ((u64)((1.0 / 60.0) * NANO_PER_SECOND))
#define FRAME_UPDATE_STEP (FRAME_DURATION / FRAME_UPDATE_COUNT)

static Vec2f CAMERA_INIT;
static Vec2f CAMERA_OFFSET;
static Vec2f CAMERA_LATENCY;
static Vec2f CAMERA;

static f32 RUN;
static f32 LEAP;
static f32 FRICTION;
static f32 DRAG;

static f32 JUMP;
static f32 GRAVITY;
static f32 DROP;
static f32 STICK;
static f32 GRAB;
static f32 RESET;

static f32 BOUNCE;

#define CAP_RECTS (1 << 6)
static Rect RECTS[CAP_RECTS];
static u32  LEN_RECTS = 0;

#define PLAYER RECTS[0]

static Vec2f PLAYER_CENTER_INIT;
static Vec2f PLAYER_SPEED = {0};
static Bool  PLAYER_CAN_JUMP;
static Bool  PLAYER_CAN_LEAP;

static Bool PAUSED = FALSE;

static u32 PROGRAM;
static i32 UNIFORM_CAMERA;
static i32 UNIFORM_WINDOW;
static i32 UNIFORM_TIME_SECONDS;
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

#define IS_DIGIT(x) (('0' <= (x)) && ((x) <= '9'))

#define IS_SPACE(x) (((x) == ' ') || ((x) == '\n'))

static Bool eq(String a, String b) {
    return (a.len == b.len) && (!memcmp(a.buffer, b.buffer, a.len));
}

static const char* string_to_buffer(String string) {
    assert((LEN_BUFFER + string.len + 1) < CAP_BUFFER);
    char* copy = &BUFFER[LEN_BUFFER];
    memcpy(copy, string.buffer, string.len);
    LEN_BUFFER += string.len;
    BUFFER[LEN_BUFFER++] = '\0';
    return copy;
}

static MemMap path_to_map(const char* path) {
    assert(path);
    const i32 file = open(path, O_RDONLY);
    assert(0 <= file);
    FileStat stat;
    assert(0 <= fstat(file, &stat));
    const MemMap map = {
        .address =
            mmap(NULL, (u32)stat.st_size, PROT_READ, MAP_SHARED, file, 0),
        .len = (u32)stat.st_size,
    };
    assert(map.address != MAP_FAILED);
    close(file);
    return map;
}

static const char* map_to_buffer(MemMap map) {
    const String string = {
        .buffer = (const char*)map.address,
        .len    = map.len,
    };
    return string_to_buffer(string);
}

static void skip_spaces(const char** buffer) {
    for (;;) {
        while (IS_SPACE(**buffer)) {
            ++(*buffer);
        }
        if (**buffer == '#') {
            ++(*buffer);
            while (**buffer != '\n') {
                if (**buffer == '\0') {
                    return;
                }
                ++(*buffer);
            }
            continue;
        }
        return;
    }
}

static String parse_key(const char** buffer) {
    String string = {
        .buffer = *buffer,
    };
    while (!IS_SPACE(**buffer)) {
        assert(**buffer != '\0');
        ++(*buffer);
    }
    string.len = (u32)(*buffer - string.buffer);
    ++(*buffer);
    return string;
}

static String parse_string(const char** buffer) {
    assert(**buffer == '"');
    ++(*buffer);
    String string = {
        .buffer = *buffer,
    };
    while (**buffer != '"') {
        assert(**buffer != '\0');
        ++(*buffer);
    }
    string.len = (u32)(*buffer - string.buffer);
    ++(*buffer);
    return string;
}

static f32 parse_f32(const char** buffer) {
    Bool negate = FALSE;
    if (**buffer == '-') {
        negate = TRUE;
        ++(*buffer);
    }
    f32 a = 0.0f;
    while (IS_DIGIT(**buffer)) {
        a = (a * 10.0f) + ((f32)(**buffer - '0'));
        ++(*buffer);
    }
    if (**buffer == '.') {
        ++(*buffer);
        f32 b = 0.0f;
        f32 c = 1.0f;
        while (IS_DIGIT(**buffer)) {
            b = (b * 10.0f) + ((f32)(**buffer - '0'));
            c *= 10.0f;
            ++(*buffer);
        }
        a += b / c;
    }
    if (negate) {
        return -a;
    }
    return a;
}

static u64 parse_u64(const char** buffer) {
    Bool negate = FALSE;
    if (**buffer == '-') {
        negate = TRUE;
        ++(*buffer);
    }
    u64 x = 0;
    while (IS_DIGIT(**buffer)) {
        x = (x * 10) + ((u64)(**buffer - '0'));
        ++(*buffer);
    }
    if (negate) {
        return -x;
    }
    return x;
}

static Vec2f parse_vec2f(const char** buffer) {
    Vec2f vec;
    vec.x = parse_f32(buffer);
    skip_spaces(buffer);
    vec.y = parse_f32(buffer);
    return vec;
}

static Rect parse_rect(const char** buffer) {
    Rect rect;
    rect.center = parse_vec2f(buffer);
    skip_spaces(buffer);
    rect.scale = parse_vec2f(buffer);
    return rect;
}

static void load_config(const char* path) {
    LEN_BUFFER = 0;
    LEN_RECTS  = 1;

    const MemMap map    = path_to_map(path);
    const char*  config = map_to_buffer(map);
    for (;;) {
        skip_spaces(&config);
        if (*config == '\0') {
            break;
        }
        const String key = parse_key(&config);
        skip_spaces(&config);
        if (eq(key, STRING("PATH_SHADER_VERT"))) {
            PATH_SHADER_VERT = string_to_buffer(parse_string(&config));
        } else if (eq(key, STRING("PATH_SHADER_FRAG"))) {
            PATH_SHADER_FRAG = string_to_buffer(parse_string(&config));
        } else if (eq(key, STRING("FRAME_UPDATE_COUNT"))) {
            FRAME_UPDATE_COUNT = parse_u64(&config);
        } else if (eq(key, STRING("CAMERA_INIT"))) {
            CAMERA_INIT = parse_vec2f(&config);
        } else if (eq(key, STRING("CAMERA_OFFSET"))) {
            CAMERA_OFFSET = parse_vec2f(&config);
        } else if (eq(key, STRING("CAMERA_LATENCY"))) {
            CAMERA_LATENCY = parse_vec2f(&config);
        } else if (eq(key, STRING("RUN"))) {
            RUN = parse_f32(&config);
        } else if (eq(key, STRING("LEAP"))) {
            LEAP = parse_f32(&config);
        } else if (eq(key, STRING("FRICTION"))) {
            FRICTION = parse_f32(&config);
        } else if (eq(key, STRING("DRAG"))) {
            DRAG = parse_f32(&config);
        } else if (eq(key, STRING("JUMP"))) {
            JUMP = parse_f32(&config);
        } else if (eq(key, STRING("GRAVITY"))) {
            GRAVITY = parse_f32(&config);
        } else if (eq(key, STRING("DROP"))) {
            DROP = parse_f32(&config);
        } else if (eq(key, STRING("BOUNCE"))) {
            BOUNCE = parse_f32(&config);
        } else if (eq(key, STRING("STICK"))) {
            STICK = parse_f32(&config);
        } else if (eq(key, STRING("GRAB"))) {
            GRAB = parse_f32(&config);
        } else if (eq(key, STRING("RESET"))) {
            RESET = parse_f32(&config);
        } else if (eq(key, STRING("PLAYER_CENTER_INIT"))) {
            PLAYER_CENTER_INIT = parse_vec2f(&config);
        } else if (eq(key, STRING("PLAYER_SCALE"))) {
            RECTS[0].scale = parse_vec2f(&config);
        } else if (eq(key, STRING("RECTS"))) {
            assert(*config == '{');
            ++config;
            skip_spaces(&config);
            while (*config != '}') {
                assert(LEN_RECTS < CAP_RECTS);
                RECTS[LEN_RECTS++] = parse_rect(&config);
                skip_spaces(&config);
            }
            ++config;
        } else {
            printf(PREFIX "unexpected key: `%.*s`\n", key.len, key.buffer);
            assert(0);
        }
    }
    assert(!munmap(map.address, map.len));
    glBufferSubData(GL_ARRAY_BUFFER,
                    sizeof(RECTS[0]),
                    (LEN_RECTS - 1) * sizeof(RECTS[0]),
                    &RECTS[1]);
    EXIT_IF_GL_ERROR()
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

__attribute__((noreturn)) static void callback_error(i32         code,
                                                     const char* error) {
    printf("%d: %s\n", code, error);
    assert(0);
}

static i32 compile_shader(const char* path, u32 shader) {
    const MemMap map    = path_to_map(path);
    const char*  source = map_to_buffer(map);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    i32 status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        glGetShaderInfoLog(shader,
                           (i32)(CAP_BUFFER - LEN_BUFFER),
                           NULL,
                           &BUFFER[LEN_BUFFER]);
        printf("%s", &BUFFER[LEN_BUFFER]);
    }
    assert(!munmap(map.address, map.len));
    return status;
}

#define BIND_BUFFER(object, array, target, usage)          \
    {                                                      \
        glGenBuffers(1, &object);                          \
        glBindBuffer(target, object);                      \
        glBufferData(target, sizeof(array), array, usage); \
        EXIT_IF_GL_ERROR();                                \
    }

static i32 compile_program(void) {
    PROGRAM               = glCreateProgram();
    const u32 shader_vert = glCreateShader(GL_VERTEX_SHADER);
    const u32 shader_frag = glCreateShader(GL_FRAGMENT_SHADER);
    {
        const i32 status = compile_shader(PATH_SHADER_VERT, shader_vert);
        if (!status) {
            return status;
        }
    }
    {
        const i32 status = compile_shader(PATH_SHADER_FRAG, shader_frag);
        if (!status) {
            return status;
        }
    }
    glAttachShader(PROGRAM, shader_vert);
    glAttachShader(PROGRAM, shader_frag);
    glLinkProgram(PROGRAM);
    i32 status = 0;
    glGetProgramiv(PROGRAM, GL_LINK_STATUS, &status);
    if (!status) {
        glGetProgramInfoLog(PROGRAM,
                            (i32)(CAP_BUFFER - LEN_BUFFER),
                            NULL,
                            &BUFFER[LEN_BUFFER]);
        printf("%s", &BUFFER[LEN_BUFFER]);
        return status;
    }
    glDeleteShader(shader_vert);
    glDeleteShader(shader_frag);
    glUseProgram(PROGRAM);
    UNIFORM_CAMERA       = glGetUniformLocation(PROGRAM, "CAMERA");
    UNIFORM_WINDOW       = glGetUniformLocation(PROGRAM, "WINDOW");
    UNIFORM_TIME_SECONDS = glGetUniformLocation(PROGRAM, "TIME_SECONDS");
    UNIFORM_PAUSED       = glGetUniformLocation(PROGRAM, "PAUSED");
    glUniform2f(UNIFORM_WINDOW, WINDOW_X, WINDOW_Y);
    glUniform1ui(UNIFORM_PAUSED, PAUSED);
    EXIT_IF_GL_ERROR();
    return status;
}

static void callback_key(GLFWwindow* window, i32 key, i32, i32 action, i32) {
    if (action != GLFW_PRESS) {
        return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE: {
        printf(PREFIX "closing window\n");
        glfwSetWindowShouldClose(window, TRUE);
        break;
    }
    case GLFW_KEY_R: {
        printf(PREFIX "loading config\n");
        load_config(PATH_CONFIG);
        glDeleteProgram(PROGRAM);
        if (!compile_program()) {
            printf(PREFIX "unable to compile shader\n");
        }
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
    return ((a_left_bottom.x < b_right_top.x) &&
            (b_left_bottom.x < a_right_top.x) &&
            (a_left_bottom.y < b_right_top.y) &&
            (b_left_bottom.y < a_right_top.y));
}

static void step(GLFWwindow* window, f32 t) {
    assert(t <= 1.0f);

    {
        const Vec2f prev = CAMERA;
        CAMERA.x -= ((CAMERA.x - CAMERA_OFFSET.x) - PLAYER.center.x) /
                    CAMERA_LATENCY.x;
        CAMERA.y -= ((CAMERA.y - CAMERA_OFFSET.y) - PLAYER.center.y) /
                    CAMERA_LATENCY.y;
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

    // NOTE: See `https://www.gamedev.net/articles/programming/general-and-gameplay-programming/swept-aabb-collision-detection-and-response-r3084/`.
    Rect left_right = {
        .center = {.y = PLAYER.center.y},
        .scale  = {.y = PLAYER.scale.y},
    };
    if (PLAYER_SPEED.x < 0.0f) {
        left_right.center.x =
            PLAYER.center.x + ((-PLAYER.scale.x + PLAYER_SPEED.x) / 2.0f);
        left_right.scale.x = -PLAYER_SPEED.x;
    } else {
        left_right.center.x =
            PLAYER.center.x + ((PLAYER.scale.x + PLAYER_SPEED.x) / 2.0f);
        left_right.scale.x = PLAYER_SPEED.x;
    }
    Rect bottom_top = {
        .center = {.x = PLAYER.center.x},
        .scale  = {.x = PLAYER.scale.x},
    };
    if (PLAYER_SPEED.y < 0.0f) {
        bottom_top.center.y =
            PLAYER.center.y + ((-PLAYER.scale.y + PLAYER_SPEED.y) / 2.0f);
        bottom_top.scale.y = -PLAYER_SPEED.y;
    } else {
        bottom_top.center.y =
            PLAYER.center.y + ((PLAYER.scale.y + PLAYER_SPEED.y) / 2.0f);
        bottom_top.scale.y = PLAYER_SPEED.y;
    }

    Bool collide_x = FALSE;
    Bool collide_y = FALSE;

    for (u32 i = 1; i < LEN_RECTS; ++i) {
        collide_x |= intersect(left_right, RECTS[i]);
        collide_y |= intersect(bottom_top, RECTS[i]);
        if (collide_x && collide_y) {
            break;
        }
    }

    {
        const Vec2f prev = PLAYER_SPEED;

        if (collide_x) {
            PLAYER_SPEED.x = -PLAYER_SPEED.x * BOUNCE;
            PLAYER_SPEED.y *= GRAB;
        }

        if (collide_y) {
            PLAYER_SPEED.x *= FRICTION;
            PLAYER_SPEED.y = -PLAYER_SPEED.y * BOUNCE;
        } else {
            PLAYER_SPEED.x *= DRAG;
        }

        PLAYER_SPEED = lerp_vec2f(prev, PLAYER_SPEED, t);
    }

    if (!collide_x) {
        PLAYER.center.x += PLAYER_SPEED.x;
    }

    if (collide_y) {
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

    PLAYER_CAN_JUMP = (collide_x || collide_y) && (PLAYER_SPEED.y <= 0.0f);
    PLAYER_CAN_LEAP = (!collide_y) && collide_x;
}

i32 main(i32 n, const char** args) {
    assert(2 <= n);
    PATH_CONFIG = args[1];

    printf("GLFW version : %s\n", glfwGetVersionString());

    assert(glfwInit());
    glfwSetErrorCallback(callback_error);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, FALSE);
    GLFWwindow* window =
        glfwCreateWindow(WINDOW_X, WINDOW_Y, WINDOW_NAME, NULL, NULL);
    if (!window) {
        glfwTerminate();
        assert(0);
    }
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
    glVertexAttribPointer(INDEX_VERTEX,
                          2,
                          GL_FLOAT,
                          FALSE,
                          sizeof(VERTICES[0]),
                          0);
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

    load_config(PATH_CONFIG);
    PLAYER.center = PLAYER_CENTER_INIT;
    CAMERA.x      = CAMERA_INIT.x + CAMERA_OFFSET.x;
    CAMERA.y      = CAMERA_INIT.y + CAMERA_OFFSET.y;

    assert(compile_program());
    glViewport(0, 0, WINDOW_X, WINDOW_Y);

    u64 prev     = now();
    u64 interval = now();
    u64 frames   = 0;
    printf("\n\n\n");
    while (!glfwWindowShouldClose(window)) {
        const u64 start = now();
        ++frames;
        if (NANO_PER_SECOND <= (start - interval)) {
            printf("\033[3A"
                   "%7.4f ms/f\n"
                   "%7lu f/s\n"
                   "                                \n",
                   (NANO_PER_SECOND / (f64)frames) / NANO_PER_MILLI,
                   frames);
            interval += NANO_PER_SECOND;
            frames = 0;
        }

        glfwPollEvents();

        u64 delta = start - prev;
        for (; FRAME_UPDATE_STEP < delta; delta -= FRAME_UPDATE_STEP) {
            step(window, 1.0f);
        }
        step(window, ((f32)delta) / FRAME_UPDATE_STEP);

        prev = start;

        glUniform2f(UNIFORM_CAMERA, CAMERA.x, CAMERA.y);
        // NOTE: See `http://the-witness.net/news/2022/02/a-shader-trick/`.
        glUniform1f(UNIFORM_TIME_SECONDS,
                    ((f32)(now() % (NANO_PER_SECOND * 10llu))) /
                        ((f32)NANO_PER_SECOND));
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(RECTS[0]), RECTS);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 6, (i32)LEN_RECTS);
        EXIT_IF_GL_ERROR()
        glfwSwapBuffers(window);
    }
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &instance_vbo);
    glDeleteProgram(PROGRAM);
    glfwTerminate();
    return 0;
}

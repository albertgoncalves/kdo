#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define GL_GLEXT_PROTOTYPES

#include <GLFW/glfw3.h>

typedef uint32_t u32;
typedef int32_t  i32;
typedef float    f32;
typedef double   f64;

typedef struct stat FileStat;

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
    u32 x, y, z;
} Vec3u;

typedef struct {
    Vec2f center, scale;
} Rect;

#define OK    0
#define ERROR 1

#define ATTRIBUTE(x) __attribute__((x))

#define STRING(literal)      \
    ((String){               \
        literal,             \
        sizeof(literal) - 1, \
    })

#define CAP_BUFFER (1 << 10)
static char BUFFER[CAP_BUFFER];
static u32  LEN_BUFFER = 0;

#define WINDOW_X 1200
#define WINDOW_Y 900

static const char* WINDOW_NAME;

#define PREFIX "  # "

#define INDEX_VERTEX    0
#define INDEX_TRANSLATE 1
#define INDEX_SCALE     2

#define MICROSECONDS 1000000.0

static const Vec2f VERTICES[] = {
    {0.5f, 0.5f},
    {0.5f, -0.5f},
    {-0.5f, -0.5f},
    {-0.5f, 0.5f},
};
static const Vec3u INDICES[] = {
    {0, 1, 3},
    {1, 2, 3},
};

static const char* PATH_CONFIG;
static const char* PATH_SHADER_VERT;
static const char* PATH_SHADER_FRAG;

static f64 FRAME_UPDATE_COUNT;
#define FRAME_DURATION    ((1.0 / 60.0) * MICROSECONDS)
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
static f32 BOUNCE;
static f32 DAMPEN;

#define CAP_RECTS (1 << 6)
static Rect RECTS[CAP_RECTS];
static u32  LEN_RECTS = 0;

#define PLAYER RECTS[0]

static Vec2f PLAYER_SPEED    = {0};
static Bool  PLAYER_CAN_JUMP = FALSE;

// NOTE: This is ugly stuff. These shouldn't *need* to be global variables.
static u32 PROGRAM;
static i32 UNIFORM_CAMERA;
static i32 UNIFORM_WINDOW;
static i32 UNIFORM_TIME_SECONDS;

#define EXIT()                                              \
    {                                                       \
        printf("%s:%s:%d\n", __FILE__, __func__, __LINE__); \
        _exit(ERROR);                                       \
    }

#define EXIT_WITH(x)                                                \
    {                                                               \
        printf("%s:%s:%d `%s`\n", __FILE__, __func__, __LINE__, x); \
        _exit(ERROR);                                               \
    }

#define EXIT_IF(condition)    \
    if (condition) {          \
        EXIT_WITH(#condition) \
    }

#define EXIT_IF_GL_ERROR()                                 \
    {                                                      \
        switch (glGetError()) {                            \
        case GL_INVALID_ENUM: {                            \
            EXIT_WITH("GL_INVALID_ENUM");                  \
        }                                                  \
        case GL_INVALID_VALUE: {                           \
            EXIT_WITH("GL_INVALID_VALUE");                 \
        }                                                  \
        case GL_INVALID_OPERATION: {                       \
            EXIT_WITH("GL_INVALID_OPERATION");             \
        }                                                  \
        case GL_INVALID_FRAMEBUFFER_OPERATION: {           \
            EXIT_WITH("GL_INVALID_FRAMEBUFFER_OPERATION"); \
        }                                                  \
        case GL_OUT_OF_MEMORY: {                           \
            EXIT_WITH("GL_OUT_OF_MEMORY");                 \
        }                                                  \
        case GL_NO_ERROR: {                                \
            break;                                         \
        }                                                  \
        }                                                  \
    }

#define IS_DIGIT(x) (('0' <= (x)) && ((x) <= '9'))

#define IS_SPACE(x) (((x) == ' ') || ((x) == '\n'))

static Bool eq(String a, String b) {
    return (a.len == b.len) && (!memcmp(a.buffer, b.buffer, a.len));
}

static const char* copy_into_buffer(String string) {
    char* copy = &BUFFER[LEN_BUFFER];
    memcpy(copy, string.buffer, string.len);
    LEN_BUFFER += string.len;
    BUFFER[LEN_BUFFER++] = '\0';
    return copy;
}

static const char* read_file(const char* path) {
    EXIT_IF(!path);
    const i32 file = open(path, O_RDONLY);
    EXIT_IF(file < 0);
    FileStat stat;
    EXIT_IF(fstat(file, &stat) < 0)
    const void* address =
        mmap(NULL, (u32)stat.st_size, PROT_READ, MAP_SHARED, file, 0);
    EXIT_IF(address == MAP_FAILED);
    const char* string = (const char*)address;
    close(file);
    return string;
}

static void skip_spaces(const char* buffer, u32* i) {
    for (;;) {
        while (IS_SPACE(buffer[*i])) {
            ++(*i);
        }
        if (buffer[*i] == '#') {
            ++(*i);
            while (buffer[(*i)++] != '\n') {
            }
            continue;
        }
        return;
    }
}

static String parse_key(const char* buffer, u32* i) {
    String string = {
        .buffer = &buffer[*i],
    };
    while (!IS_SPACE(buffer[*i])) {
        EXIT_IF(buffer[*i] == '\0');
        ++(*i);
    }
    string.len = (u32)(&buffer[(*i)++] - string.buffer);
    return string;
}

static String parse_string(const char* buffer, u32* i) {
    EXIT_IF(buffer[(*i)++] != '"');
    String string = {
        .buffer = &buffer[*i],
    };
    while (buffer[*i] != '"') {
        EXIT_IF(buffer[*i] == '\0');
        ++(*i);
    }
    string.len = (u32)(&buffer[(*i)++] - string.buffer);
    return string;
}

static f32 parse_f32(const char* buffer, u32* i) {
    Bool negate = FALSE;
    if (buffer[*i] == '-') {
        negate = TRUE;
        ++(*i);
    }
    f32 a = 0;
    while (IS_DIGIT(buffer[*i])) {
        a = (a * 10) + ((f32)(buffer[(*i)++] - '0'));
    }
    EXIT_IF(buffer[(*i)++] != '.');
    f32 b = 0.0f;
    f32 c = 1.0f;
    while (IS_DIGIT(buffer[*i])) {
        b = (b * 10.0f) + ((f32)(buffer[(*i)++] - '0'));
        c *= 10.0f;
    }
    a += b / c;
    if (negate) {
        return -a;
    }
    return a;
}

static f64 parse_f64(const char* buffer, u32* i) {
    Bool negate = FALSE;
    if (buffer[*i] == '-') {
        negate = TRUE;
        ++(*i);
    }
    f64 a = 0;
    while (IS_DIGIT(buffer[*i])) {
        a = (a * 10) + ((f64)(buffer[(*i)++] - '0'));
    }
    EXIT_IF(buffer[(*i)++] != '.');
    f64 b = 0.0;
    f64 c = 1.0;
    while (IS_DIGIT(buffer[*i])) {
        b = (b * 10.0) + ((f64)(buffer[(*i)++] - '0'));
        c *= 10.0;
    }
    a += b / c;
    if (negate) {
        return -a;
    }
    return a;
}

static Vec2f parse_vec2f(const char* buffer, u32* i) {
    Vec2f vec;
    vec.x = parse_f32(buffer, i);
    skip_spaces(buffer, i);
    vec.y = parse_f32(buffer, i);
    return vec;
}

static Rect parse_rect(const char* buffer, u32* i) {
    Rect rect;
    rect.center = parse_vec2f(buffer, i);
    skip_spaces(buffer, i);
    rect.scale = parse_vec2f(buffer, i);
    return rect;
}

static void load_config(const char* path) {
    const char* config = read_file(path);

    LEN_BUFFER = 0;
    LEN_RECTS  = 0;
    for (u32 i = 0;;) {
        skip_spaces(config, &i);
        if (config[i] == '\0') {
            break;
        }
        String key = parse_key(config, &i);
        skip_spaces(config, &i);
        if (eq(key, STRING("WINDOW_NAME"))) {
            WINDOW_NAME = copy_into_buffer(parse_string(config, &i));
        } else if (eq(key, STRING("PATH_SHADER_VERT"))) {
            PATH_SHADER_VERT = copy_into_buffer(parse_string(config, &i));
        } else if (eq(key, STRING("PATH_SHADER_FRAG"))) {
            PATH_SHADER_FRAG = copy_into_buffer(parse_string(config, &i));
        } else if (eq(key, STRING("FRAME_UPDATE_COUNT"))) {
            FRAME_UPDATE_COUNT = parse_f64(config, &i);
        } else if (eq(key, STRING("CAMERA_INIT"))) {
            CAMERA_INIT = parse_vec2f(config, &i);
        } else if (eq(key, STRING("CAMERA_OFFSET"))) {
            CAMERA_OFFSET = parse_vec2f(config, &i);
        } else if (eq(key, STRING("CAMERA_LATENCY"))) {
            CAMERA_LATENCY = parse_vec2f(config, &i);
        } else if (eq(key, STRING("RUN"))) {
            RUN = parse_f32(config, &i);
        } else if (eq(key, STRING("LEAP"))) {
            LEAP = parse_f32(config, &i);
        } else if (eq(key, STRING("FRICTION"))) {
            FRICTION = parse_f32(config, &i);
        } else if (eq(key, STRING("DRAG"))) {
            DRAG = parse_f32(config, &i);
        } else if (eq(key, STRING("JUMP"))) {
            JUMP = parse_f32(config, &i);
        } else if (eq(key, STRING("GRAVITY"))) {
            GRAVITY = parse_f32(config, &i);
        } else if (eq(key, STRING("DROP"))) {
            DROP = parse_f32(config, &i);
        } else if (eq(key, STRING("BOUNCE"))) {
            BOUNCE = parse_f32(config, &i);
        } else if (eq(key, STRING("DAMPEN"))) {
            DAMPEN = parse_f32(config, &i);
        } else if (eq(key, STRING("RECTS"))) {
            EXIT_IF(config[i] != '{');
            ++i;
            skip_spaces(config, &i);
            while (config[i] != '}') {
                EXIT_IF(CAP_RECTS <= LEN_RECTS);
                RECTS[LEN_RECTS++] = parse_rect(config, &i);
                skip_spaces(config, &i);
            }
            ++i;
        } else {
            printf(PREFIX "unexpected key: `%.*s`\n", key.len, key.buffer);
            EXIT();
        }
    }
    CAMERA.x = CAMERA_INIT.x + CAMERA_OFFSET.x;
    CAMERA.y = CAMERA_INIT.y + CAMERA_OFFSET.y;
}

static f64 now(void) {
    return glfwGetTime() * MICROSECONDS;
}

ATTRIBUTE(noreturn) static void callback_error(i32 code, const char* error) {
    printf("%d: %s\n", code, error);
    _exit(ERROR);
}

static i32 compile_shader(const char* source, u32 shader) {
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    i32 status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        glGetShaderInfoLog(shader, CAP_BUFFER, NULL, &BUFFER[LEN_BUFFER]);
        printf("%s", &BUFFER[LEN_BUFFER]);
    }
    return status;
}

#define BIND_BUFFER(object, array, target)                          \
    {                                                               \
        glGenBuffers(1, &object);                                   \
        glBindBuffer(target, object);                               \
        glBufferData(target, sizeof(array), array, GL_STATIC_DRAW); \
        EXIT_IF_GL_ERROR();                                         \
    }

static i32 compile_program(void) {
    PROGRAM               = glCreateProgram();
    const u32 shader_vert = glCreateShader(GL_VERTEX_SHADER);
    const u32 shader_frag = glCreateShader(GL_FRAGMENT_SHADER);
    {
        const i32 status =
            compile_shader(read_file(PATH_SHADER_VERT), shader_vert);
        if (!status) {
            return status;
        }
    }
    {
        const i32 status =
            compile_shader(read_file(PATH_SHADER_FRAG), shader_frag);
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
        glGetProgramInfoLog(PROGRAM, CAP_BUFFER, NULL, &BUFFER[LEN_BUFFER]);
        printf("%s", &BUFFER[LEN_BUFFER]);
        return status;
    }
    glDeleteShader(shader_vert);
    glDeleteShader(shader_frag);
    glUseProgram(PROGRAM);
    UNIFORM_CAMERA       = glGetUniformLocation(PROGRAM, "CAMERA");
    UNIFORM_WINDOW       = glGetUniformLocation(PROGRAM, "WINDOW");
    UNIFORM_TIME_SECONDS = glGetUniformLocation(PROGRAM, "TIME_SECONDS");
    glUniform2f(UNIFORM_WINDOW, WINDOW_X, WINDOW_Y);
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
    case GLFW_KEY_W: {
        if (PLAYER_CAN_JUMP) {
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                PLAYER_SPEED.x += LEAP;
            }
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                PLAYER_SPEED.x -= LEAP;
            }
            PLAYER_SPEED.y += JUMP;
        }
        break;
    }
    }
}

i32 main(i32 n, const char** args) {
    EXIT_IF(n < 2);
    PATH_CONFIG = args[1];
    load_config(PATH_CONFIG);

    printf("GLFW version : %s\n", glfwGetVersionString());

    EXIT_IF(!glfwInit());
    glfwSetErrorCallback(callback_error);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, FALSE);
    GLFWwindow* window =
        glfwCreateWindow(WINDOW_X, WINDOW_Y, WINDOW_NAME, NULL, NULL);
    if (!window) {
        glfwTerminate();
        EXIT();
    }
    glfwSetKeyCallback(window, callback_key);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glClearColor(0.125f, 0.125f, 0.125f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    EXIT_IF_GL_ERROR()

    u32 vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    EXIT_IF_GL_ERROR();

    u32 vbo;
    BIND_BUFFER(vbo, VERTICES, GL_ARRAY_BUFFER);

    u32 ebo;
    BIND_BUFFER(ebo, INDICES, GL_ELEMENT_ARRAY_BUFFER);
    glEnableVertexAttribArray(INDEX_VERTEX);
    glVertexAttribPointer(INDEX_VERTEX,
                          2,
                          GL_FLOAT,
                          FALSE,
                          sizeof(VERTICES[0]),
                          0);
    EXIT_IF_GL_ERROR();

    u32 instance_vbo;
    BIND_BUFFER(instance_vbo, RECTS, GL_ARRAY_BUFFER);

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

    EXIT_IF(!compile_program());
    glViewport(0, 0, WINDOW_X, WINDOW_Y);

    f64 prev  = now();
    f64 delta = 0.0;
    while (!glfwWindowShouldClose(window)) {
        const f64 start = now();
        delta += start - prev;
        while (FRAME_UPDATE_STEP < delta) {
            glfwPollEvents();
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
            PLAYER.center.y += PLAYER_SPEED.y;
            if (PLAYER.center.y <= 0.0f) {
                PLAYER_SPEED.x *= FRICTION;
                PLAYER.center.y = 0.0f;
                PLAYER_SPEED.y  = -PLAYER_SPEED.y * BOUNCE;
                if (PLAYER_SPEED.y < DAMPEN) {
                    PLAYER_SPEED.y = 0.0f;
                }
                PLAYER_CAN_JUMP = TRUE;
            } else {
                PLAYER_SPEED.x *= DRAG;
                PLAYER_CAN_JUMP = FALSE;
            }
            PLAYER.center.x += PLAYER_SPEED.x;
            CAMERA.x -= ((CAMERA.x - CAMERA_OFFSET.x) - PLAYER.center.x) /
                        CAMERA_LATENCY.x;
            CAMERA.y -= ((CAMERA.y - CAMERA_OFFSET.y) - PLAYER.center.y) /
                        CAMERA_LATENCY.y;
            delta -= FRAME_UPDATE_STEP;
        }
        prev = start;

        glUniform2f(UNIFORM_CAMERA, CAMERA.x, CAMERA.y);
        glUniform1f(UNIFORM_TIME_SECONDS, (f32)glfwGetTime());
#if 1
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(RECTS[0]), RECTS);
#else
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        LEN_RECTS * sizeof(RECTS[0]),
                        RECTS);
#endif
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElementsInstanced(GL_TRIANGLES,
                                6,
                                GL_UNSIGNED_INT,
                                INDEX_VERTEX,
                                (i32)LEN_RECTS);
        EXIT_IF_GL_ERROR()
        glfwSwapBuffers(window);

        const f64 elapsed = now() - start;
        if (elapsed < FRAME_DURATION) {
            usleep((u32)(FRAME_DURATION - elapsed));
        }
    }
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &instance_vbo);
    glDeleteProgram(PROGRAM);
    glfwTerminate();
    return OK;
}

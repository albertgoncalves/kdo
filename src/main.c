#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
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
    f32 x, y;
} Vec2f;

typedef struct {
    u32 x, y, z;
} Vec3u;

typedef struct {
    Vec2f center;
    Vec2f scale;
} Rect;

#define OK    0
#define ERROR 1

#define ATTRIBUTE(x) __attribute__((x))

#define CAP_BUFFER (1 << 10)
static char BUFFER[CAP_BUFFER];

#define WIDTH  1200
#define HEIGHT 900

#define PREFIX "  # "

#define INDEX_VERTEX    0
#define INDEX_TRANSLATE 1
#define INDEX_SCALE     2

#define MICROSECONDS 1000000.0

static const Vec2f VERTICES[] = {
    {1.0f, 1.0f},
    {1.0f, -1.0f},
    {-1.0f, -1.0f},
    {-1.0f, 1.0f},
};
static const Vec3u INDICES[] = {
    {0, 1, 3},
    {1, 2, 3},
};

static const char* PATH_SHADER_VERT;
static const char* PATH_SHADER_FRAG;

#define FRAME_UPDATE_COUNT 8.0
#define FRAME_DURATION     ((1.0 / 60.0) * MICROSECONDS)
#define FRAME_UPDATE_STEP  (FRAME_DURATION / FRAME_UPDATE_COUNT)

static f32 CAMERA_X = 0.0f;
static f32 CAMERA_Y = 0.0f;

#define CAMERA_LATENCY_X 125.0f
#define CAMERA_LATENCY_Y 250.0f


#define RUN      0.1325f
#define FRICTION 0.9675f
#define DRAG     0.945f

#define JUMP    7.5f
#define GRAVITY 0.0525f
#define DROP    (GRAVITY * 7.5f)

static f32 PLAYER_SPEED_X = 0.0f;
static f32 PLAYER_SPEED_Y = 0.0f;

static Bool PLAYER_CAN_JUMP = FALSE;

#define PLAYER_CENTER_X 0.0f
#define PLAYER_CENTER_Y 0.0f

#define PLAYER_SCALE_X 50.0f
#define PLAYER_SCALE_Y PLAYER_SCALE_X

static Rect RECTS[] = {
    {{PLAYER_CENTER_X, PLAYER_CENTER_Y}, {PLAYER_SCALE_X, PLAYER_SCALE_Y}},
    {{0.0f, -60.0f}, {1200.0f, 10.0f}},
};

#define CAP_RECTS (sizeof(RECTS) / sizeof(RECTS[0]))

#define PLAYER RECTS[0]

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

#define EXIT_WITH(x)                                               \
    {                                                              \
        printf("%s:%s:%d\n%s\n", __FILE__, __func__, __LINE__, x); \
        _exit(ERROR);                                              \
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

static const char* read_string(const char* path) {
    i32 file = open(path, O_RDONLY);
    EXIT_IF(file < 0);
    FileStat stat;
    EXIT_IF(fstat(file, &stat) < 0)
    void* address =
        mmap(NULL, (u32)stat.st_size, PROT_READ, MAP_SHARED, file, 0);
    EXIT_IF(address == MAP_FAILED);
    const char* string = (const char*)address;
    close(file);
    return string;
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
        glGetShaderInfoLog(shader, CAP_BUFFER, NULL, BUFFER);
        printf("%s", BUFFER);
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
            compile_shader(read_string(PATH_SHADER_VERT), shader_vert);
        if (!status) {
            return status;
        }
    }
    {
        const i32 status =
            compile_shader(read_string(PATH_SHADER_FRAG), shader_frag);
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
        glGetProgramInfoLog(PROGRAM, CAP_BUFFER, NULL, BUFFER);
        printf("%s", BUFFER);
        return status;
    }
    glDeleteShader(shader_vert);
    glDeleteShader(shader_frag);
    glUseProgram(PROGRAM);
    UNIFORM_CAMERA       = glGetUniformLocation(PROGRAM, "CAMERA");
    UNIFORM_WINDOW       = glGetUniformLocation(PROGRAM, "WINDOW");
    UNIFORM_TIME_SECONDS = glGetUniformLocation(PROGRAM, "TIME_SECONDS");
    glUniform2f(UNIFORM_WINDOW, WIDTH, HEIGHT);
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
        glDeleteProgram(PROGRAM);
        if (!compile_program()) {
            printf(PREFIX "unable to compile shader\n");
        } else {
            printf(PREFIX "shaders re-compiled\n");
        }
        break;
    }
    case GLFW_KEY_W: {
        if (PLAYER_CAN_JUMP) {
            PLAYER_SPEED_Y += JUMP;
            PLAYER_CAN_JUMP = FALSE;
        }
        break;
    }
    }
}

i32 main(i32 n, const char** args) {
    EXIT_IF(n < 3);
    PATH_SHADER_VERT = args[1];
    PATH_SHADER_FRAG = args[2];

    printf("GLFW version : %s\n", glfwGetVersionString());

    EXIT_IF(!glfwInit());
    glfwSetErrorCallback(callback_error);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, FALSE);
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "float", NULL, NULL);
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

    u32 array_vertex;
    glGenVertexArrays(1, &array_vertex);
    glBindVertexArray(array_vertex);
    EXIT_IF_GL_ERROR();

    u32 buffer_vertex;
    BIND_BUFFER(buffer_vertex, VERTICES, GL_ARRAY_BUFFER);

    u32 buffer_element;
    BIND_BUFFER(buffer_element, INDICES, GL_ELEMENT_ARRAY_BUFFER);
    glEnableVertexAttribArray(INDEX_VERTEX);
    glVertexAttribPointer(INDEX_VERTEX,
                          2,
                          GL_FLOAT,
                          FALSE,
                          sizeof(VERTICES[0]),
                          0);
    EXIT_IF_GL_ERROR();

    u32 buffer_instance;
    BIND_BUFFER(buffer_instance, RECTS, GL_ARRAY_BUFFER);

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
    glViewport(0, 0, WIDTH, HEIGHT);

    f64 prev  = now();
    f64 delta = 0.0;
    while (!glfwWindowShouldClose(window)) {
        const f64 start = now();
        delta += start - prev;
        while (FRAME_UPDATE_STEP < delta) {
            glfwPollEvents();
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                PLAYER_SPEED_X += RUN;
            }
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                PLAYER_SPEED_X -= RUN;
            }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
                PLAYER_SPEED_Y -= DROP;
            } else {
                PLAYER_SPEED_Y -= GRAVITY;
            }
            PLAYER.center.y += PLAYER_SPEED_Y;
            if (PLAYER.center.y <= 0.0f) {
                PLAYER_SPEED_X *= FRICTION;
                PLAYER.center.y = 0.0f;
                PLAYER_SPEED_Y  = 0.0f;
                PLAYER_CAN_JUMP = TRUE;
            } else {
                PLAYER_SPEED_X *= DRAG;
            }
            PLAYER.center.x += PLAYER_SPEED_X;
            CAMERA_X -= (CAMERA_X - PLAYER.center.x) / CAMERA_LATENCY_X;
            CAMERA_Y -= (CAMERA_Y - PLAYER.center.y) / CAMERA_LATENCY_Y;
            delta -= FRAME_UPDATE_STEP;
        }
        prev = start;

        glUniform2f(UNIFORM_CAMERA, CAMERA_X, CAMERA_Y);
        glUniform1f(UNIFORM_TIME_SECONDS, (f32)glfwGetTime());
#if 1
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(RECTS[0]), RECTS);
#else
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        CAP_RECTS * sizeof(RECTS[0]),
                        RECTS);
#endif
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElementsInstanced(GL_TRIANGLES,
                                6,
                                GL_UNSIGNED_INT,
                                INDEX_VERTEX,
                                CAP_RECTS);
        EXIT_IF_GL_ERROR()
        glfwSwapBuffers(window);

        const f64 elapsed = now() - start;
        if (elapsed < FRAME_DURATION) {
            usleep((u32)(FRAME_DURATION - elapsed));
        }
    }
    glDeleteVertexArrays(1, &array_vertex);
    glDeleteBuffers(1, &buffer_vertex);
    glDeleteBuffers(1, &buffer_element);
    glDeleteBuffers(1, &buffer_instance);
    glDeleteProgram(PROGRAM);
    glfwTerminate();
    return OK;
}

#include "glad.c"

#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

void debugFunction(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION) {
        printf("%s\n", message);
    }
}

int main() {
    unsigned viewportW = 1280;
    unsigned viewportH = 720;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(viewportW, viewportH, "PBO test", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc*) glfwGetProcAddress);

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugFunction, NULL);

    // clang-format off
    const float vertexData[] = {
       -1.0f,-1.0f, 1.0f, 0.0f, 0.0f,
        1.0f,-1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f,

        1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
       -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
       -1.0f,-1.0f, 1.0f, 0.0f, 0.0f,
    };
    // clang-format on
    
    // Setup fullscreen rect

    GLuint VBO;
    glCreateBuffers(1, &VBO);
    glNamedBufferStorage(VBO, sizeof(vertexData), vertexData, 0);
    GLuint VAO;
    glCreateVertexArrays(1, &VAO);
    glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(float) * 5);
    glEnableVertexArrayAttrib(VAO, 0);
    glVertexArrayAttribBinding(VAO, 0, 0);
    glVertexArrayAttribFormat(VAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glEnableVertexArrayAttrib(VAO, 1);
    glVertexArrayAttribBinding(VAO, 1, 0);
    glVertexArrayAttribFormat(VAO, 1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 2);

    // Setup shaders

    char const* vertexShaderSource =
        "#version 460 core\n"
        "layout (location = 0) in vec2 vPos;\n"
        "layout (location = 1) in vec3 vColor;\n"
        "layout (location = 0) out vec3 fColor;\n"
        "void main() {\n"
        "    fColor = vColor;\n"
        "    gl_Position = vec4(vPos, 0.0f, 1.0f);\n"
        "}";

    char const* fragmentShaderSource =
        "#version 460 core\n"
        "layout (location = 0) in vec3 fColor;\n"
        "layout (location = 0) out vec4 color;\n"
        "void main() {\n"
        "    color = vec4(fColor, 1.0f);\n"
        "}";

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    GLuint drawProgram = glCreateProgram();
    glAttachShader(drawProgram, vertexShader);
    glAttachShader(drawProgram, fragmentShader);
    glLinkProgram(drawProgram);
    glDetachShader(drawProgram, vertexShader);
    glDetachShader(drawProgram, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Create PBO
    // Longer queue size helps avoid stalls at the cost of higher memory usage and latency

    const unsigned queueSize = 4;
    unsigned drawI = 0;

    GLuint PBO = 0;
    glCreateBuffers(1, &PBO);
    const unsigned subbufferSize = viewportW * viewportH * 4 * sizeof(GLubyte);
    glNamedBufferStorage(PBO, subbufferSize * queueSize, NULL, 0);

    // Create render texture array

    GLuint drawColorTextureArray = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &drawColorTextureArray);
    glTextureStorage3D(drawColorTextureArray, 1, GL_RGBA8, viewportW, viewportH, queueSize);

    // Create FBO

    GLuint drawFramebuffer;
    glCreateFramebuffers(1, &drawFramebuffer);
    for (unsigned i = 0; i < queueSize; i++) {
        glNamedFramebufferTextureLayer(drawFramebuffer, GL_COLOR_ATTACHMENT0 + i, drawColorTextureArray, 0, i);
    }

    while (!glfwWindowShouldClose(window)) {
        glBindFramebuffer(GL_FRAMEBUFFER, drawFramebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + drawI);
        glReadBuffer(GL_COLOR_ATTACHMENT0 + drawI);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(drawProgram);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Synchronous version -- will cause stalls
#if 0
        GLubyte* buffer = malloc(subbufferSize);
        assert(buffer);
        glReadnPixels(0, 0, viewportW, viewportH, GL_RGBA, GL_UNSIGNED_BYTE, subbufferSize, buffer);
        free(buffer);
#endif
        // Asynchronous version -- does not cause stalls
#if 0
        const unsigned copyOffset = subbufferSize * drawI;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, PBO);
        glReadnPixels(0, 0, viewportW, viewportH, GL_RGBA, GL_UNSIGNED_BYTE, subbufferSize, copyOffset);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#endif
        // Asynchronous version -- performance issues (???)
#if 1
        const unsigned copyOffset = subbufferSize * drawI;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, PBO);
        glGetTextureSubImage(drawColorTextureArray, 0, 0, 0, drawI, viewportW, viewportH, 1, GL_BGRA, GL_UNSIGNED_BYTE, subbufferSize, copyOffset);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#endif

        drawI = (drawI + 1) % queueSize;

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, viewportW, viewportH, 0, 0, viewportW, viewportH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteProgram(drawProgram);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &PBO);
    glDeleteTextures(1, &drawColorTextureArray);
    glDeleteFramebuffers(1, &drawFramebuffer);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "../../../imrichtext.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include <cstdlib>
#include <unordered_map>
#include <map>

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
//#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
//#pragma comment(lib, "legacy_stdio_definitions")
//#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

#include <string_view>
#include <cstring>
#include <cctype>
#include <stack>
#include <charconv>
#include <string>

class Application
{
public:

    Application()
    {
#ifdef IM_RICHTEXT_TARGET_IMGUI
        glfwSetErrorCallback(glfw_error_callback);
        if (!glfwInit())
            std::exit(0);

        // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
        const char* glsl_version = "#version 100";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
        const char* glsl_version = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
        const char* glsl_version = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
        //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Create window with graphics context
        m_window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+OpenGL3 example", nullptr, nullptr);
        if (m_window == nullptr)
            std::exit(0);
        glfwMakeContextCurrent(m_window);
        glfwSwapInterval(1); // Enable vsync

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(m_window, true);
#ifdef __EMSCRIPTEN__
        ImGui_ImplGlfw_InstallEmscriptenCallbacks(m_window, "#canvas");
#endif
        ImGui_ImplOpenGL3_Init(glsl_version);
#endif
    }

    int run()
    {
        auto id1 = ImRichText::CreateRichText("<marquee>This is moving...</marquee>"
            "<blink>This is blinking</blink>"
            "<meter value='3' max='10'></meter>"
            "<s><q>Quotation </q><cite>Citation</cite></s>"
            "<br>Powered by: <a href='https://github.com/ajax-crypto/ImRichText'>ImRichText</a>"
            "<ul style='font-size: 36px;'><li>item</li><li>item</li></ul>");

        auto id2 = ImRichText::CreateRichText(
            "2<sup>2</sup> equals 4  <hr style=\"height: 4px; color: sienna;\"/>"
            "<p style=\"color: rgb(150, 0, 0); border: 2px solid gray;\">This is a paragraph <b>bold <i>italics</i> bold2 </b></p>"
            "<h1 style=\"color: darkblue;\">Heading&Tab;</h1>"
            "<p style='background: linear-gradient(red, yellow, green); color: white;'>Multi-line <br> Text on gradient</p><br/>"
            "<center>This needlessly long piece of text is a test of word wrapping inside tags, with alignment</center><br/>"
            "<mark>This is highlighted! <small>This is small...</small></mark>"
            );

        ImRichText::DefaultConfigParams params;
        params.DefaultFontSize = 24.f;
        params.FontScale = 1.5f;
        params.FontLoadFlags = ImRichText::FLT_HasSmall | ImRichText::FLT_Proportional | ImRichText::FLT_HasH1 | 
            ImRichText::FLT_HasSuperscript;
        auto config = ImRichText::GetDefaultConfig(params);
        config->ListItemBullet = ImRichText::BulletType::Arrow;
        std::ignore = ImRichText::FindFontFile("Segoe UI", ImRichText::FontType::FT_Normal);

#ifdef IM_RICHTEXT_TARGET_IMGUI
        ImRichText::ImGuiRenderer renderer{ *config };
        ImRichText::ImGuiPlatform platform;

        config->Renderer = &renderer;
        config->Platform = &platform;
#elif defined(IM_RICHTEXT_TARGET_BLEND2D)
        BLImage img(800, 800, BL_FORMAT_PRGB32);
        BLContext context{ img };
        Blend2DRenderer renderer{ context };
        config->Renderer = &renderer;
#endif

#ifdef _DEBUG
        config->DebugContents[ImRichText::ContentTypeLine] = ImColor{ 255, 0, 0 };
        config->DebugContents[ImRichText::ContentTypeSegment] = ImColor{ 0, 255, 0 };
#endif
        config->Scale = 1.5f;
        ImRichText::PushConfig(*config);

#ifdef IM_RICHTEXT_TARGET_IMGUI
        ImVec4 clear_color = ImVec4(1.f, 1.f, 1.f, 1.00f);

        // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
        io.IniFilename = nullptr;
        EMSCRIPTEN_MAINLOOP_BEGIN
#else
        while (!glfwWindowShouldClose(m_window))
#endif
        {
            // Poll and handle events (inputs, window resize, etc.)
            // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
            // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
            // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
            // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
            glfwPollEvents();
            if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0)
            {
                ImGui_ImplGlfw_Sleep(10);
                continue;
            }

            int width, height;
            glfwGetWindowSize(m_window, &width, &height);

            // Start the Dear ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowSize(ImVec2{ (float)width, (float)height }, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2{ 0, 0 });

            // Render here
            if (ImGui::Begin("main-window", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
            {
                if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_O)) ImRichText::ToggleOverlay();

#endif
                ImRichText::GetCurrentConfig()->DefaultBgColor = ImColor{ 255, 255, 255 };
                //ImRichText::Show(id1);
                ImRichText::Show(id2, ImVec2{ 500.f, 1000.f });

#ifdef IM_RICHTEXT_TARGET_IMGUI
            }

            ImGui::End();

            // Rendering
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(m_window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(m_window);
        }
#ifdef __EMSCRIPTEN__
        EMSCRIPTEN_MAINLOOP_END;
#endif

        // Cleanup
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(m_window);
        glfwTerminate();
#else
        img.writeToFile("rtf.png");
#endif

        return 0;
    }

private:

    GLFWwindow* m_window = nullptr;
    ImFont* m_defaultFont = nullptr;
};

#if !defined(_DEBUG) && defined(WIN32)
int CALLBACK WinMain(
    HINSTANCE   hInstance,
    HINSTANCE   hPrevInstance,
    LPSTR       lpCmdLine,
    int         nCmdShow
)
#else
int main(int argc, char** argv)
#endif
{
    Application app;
    return app.run();
}

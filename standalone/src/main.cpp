#include <imgui.h>
#include "imgui_impl_glfw_gl3.h"
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <memory>
#include "../nfd/nfd.h"
#include <sys/stat.h>

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#endif

#include "../../server/TracyBadVersion.hpp"
#include "../../server/TracyFileRead.hpp"
#include "../../server/TracyView.hpp"
#include "../../server/TracyWorker.hpp"

#include "Arimo.hpp"

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Error %d: %s\n", error, description);
}

void OpenWebpage( const char* url )
{
#ifdef _WIN32
    ShellExecuteA( nullptr, nullptr, url, nullptr, nullptr, 0 );
#else
    char buf[1024];
    sprintf( buf, "xdg-open %s", url );
    system( buf );
#endif
}

int main( int argc, char** argv )
{
    std::unique_ptr<tracy::View> view;
    int badVer = 0;

    if( argc == 2 )
    {
        auto f = std::unique_ptr<tracy::FileRead>( tracy::FileRead::Open( argv[1] ) );
        if( f )
        {
            view = std::make_unique<tracy::View>( *f );
        }
    }

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1650, 960, "Tracy server", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    gl3wInit();

    float dpiScale = 1.f;
#ifdef _WIN32
    typedef UINT(*GDFS)(void);
    GDFS getDpiForSystem = nullptr;
    HMODULE dll = GetModuleHandleW(L"user32.dll");
    if (dll != INVALID_HANDLE_VALUE)
        getDpiForSystem = (GDFS)GetProcAddress(dll, "GetDpiForSystem");
    if (getDpiForSystem)
        dpiScale = getDpiForSystem() / 96.f;
#endif

    // Setup ImGui binding
    ImGui::CreateContext();
    ImGui_ImplGlfwGL3_Init(window, true);

    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x03BC, 0x03BC, // micro
        0,
    };

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromMemoryCompressedTTF( tracy::Arimo_compressed_data, tracy::Arimo_compressed_size, 15.0f * dpiScale, nullptr, ranges );

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowBorderSize = 1.f;
    style.FrameBorderSize = 1.f;
    style.FrameRounding = 5.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4( 0.11f, 0.11f, 0.08f, 0.94f );
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4( 1, 1, 1, 0.03f );

    ImVec4 clear_color = ImColor(114, 144, 154);

    char addr[1024] = { "127.0.0.1" };

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplGlfwGL3_NewFrame();

        if( !view )
        {
            ImGui::Begin( "Tracy server", nullptr, ImGuiWindowFlags_AlwaysAutoResize );
            if( ImGui::SmallButton( "Homepage" ) )
            {
                OpenWebpage( "https://bitbucket.org/wolfpld/tracy" );
            }
            ImGui::SameLine();
            if( ImGui::SmallButton( "Tutorial" ) )
            {
                OpenWebpage( "https://www.youtube.com/watch?v=fB5B46lbapc" );
            }
            ImGui::Separator();
            ImGui::Text( "Connect to client" );
            ImGui::InputText( "Address", addr, 1024 );
            if( ImGui::Button( "Connect" ) && *addr )
            {
                view = std::make_unique<tracy::View>( addr );
            }
            ImGui::Separator();
            if( ImGui::Button( "Open saved trace" ) )
            {
                nfdchar_t* fn;
                auto res = NFD_OpenDialog( "tracy", nullptr, &fn );
                if( res == NFD_OKAY )
                {
                    try
                    {
                        auto f = std::unique_ptr<tracy::FileRead>( tracy::FileRead::Open( fn ) );
                        if( f )
                        {
                            view = std::make_unique<tracy::View>( *f );
                        }
                    }
                    catch( const tracy::UnsupportedVersion& e )
                    {
                        badVer = e.version;
                    }
                    catch( const tracy::NotTracyDump& e )
                    {
                        badVer = -1;
                    }
                }
            }

            tracy::BadVersion( badVer );

            ImGui::End();
        }
        else
        {
            if( !view->Draw() )
            {
                view.reset();
            }
        }
        auto& progress = tracy::Worker::GetLoadProgress();
        auto totalProgress = progress.total.load( std::memory_order_relaxed );
        if( totalProgress != 0 )
        {
            ImGui::OpenPopup( "Loading trace..." );
        }
        if( ImGui::BeginPopupModal( "Loading trace..." ) )
        {
            auto currProgress = progress.progress.load( std::memory_order_relaxed );
            if( totalProgress == 0 )
            {
                ImGui::CloseCurrentPopup();
                totalProgress = currProgress;
            }
            ImGui::ProgressBar( float( currProgress ) / totalProgress, ImVec2( 200 * dpiScale, 0 ) );
            ImGui::EndPopup();
        }

        // Rendering
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();

    return 0;
}

#ifdef _WIN32
#include <stdlib.h>
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmd, int nCmd )
{
    return main( __argc, __argv );
}
#endif

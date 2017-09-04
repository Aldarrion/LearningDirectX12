#include <Tutorial1PCH.h>
#include <DirectX12Tutorial.h>

#include <Tutorial1.h>

static uint32_t g_ClientWidth = 1280;
static uint32_t g_ClientHeight = 720;

int CALLBACK wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow )
{
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc );
    Application app(hInstance, argc, const_cast<const wchar_t**>( argv ));
    
    // Parse command-line arguments
    for (size_t i = 0; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"-w") == 0 || wcscmp(argv[i], L"--width") == 0)
        {
            g_ClientWidth = wcstol(argv[++i], nullptr, 10);
        }
        if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--height") == 0)
        {
            g_ClientHeight = wcstol(argv[++i], nullptr, 10);
        }
    }

    Tutorial1 game(g_ClientWidth, g_ClientHeight, L"Tutorial1");

    int ret = app.Run();

    // Free memory allocated by CommandLineToArgvW
    LocalFree(argv);

    return ret;
}
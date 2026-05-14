#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include "ViewModels/AppState.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::epyks_winui::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        window = make<MainWindow>();
        window.Activate();

        // Get HWND safely after activation and store in AppState for file pickers
        try
        {
            auto nativeWindow = window.try_as<IWindowNative>();
            if (nativeWindow)
            {
                HWND hWnd = nullptr;
                nativeWindow->get_WindowHandle(&hWnd);
                ::epyks_winui::GetAppState().mainWindowHwnd = hWnd;
            }
        }
        catch (...) {}
    }
}

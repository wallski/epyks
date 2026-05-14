#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Pages/LoginPage.xaml.h"
#include "Pages/ChatPage.xaml.h"
#include "ViewModels/AppState.h"
#include <microsoft.ui.xaml.window.h>

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Composition::SystemBackdrops;
#include <winrt/Microsoft.UI.Xaml.Interop.h>

namespace winrt::epyks_winui::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        
        auto windowNative{ this->try_as<::IWindowNative>() };
        if (windowNative)
        {
            HWND hWnd{ 0 };
            windowNative->get_WindowHandle(&hWnd);
            ::epyks_winui::GetAppState().mainWindowHwnd = hWnd;
        }

        this->AppWindow().Resize({ 1000, 680 });
        SetupTitleBar();

        NavigateToLogin();
    }

    void MainWindow::SetupTitleBar()
    {
        // Extend content into title bar area for custom look
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        // Use modern WinUI 3 SystemBackdrop API for Mica
        winrt::Microsoft::UI::Xaml::Media::MicaBackdrop backdrop;
        backdrop.Kind(winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind::BaseAlt);
        this->SystemBackdrop(backdrop);
    }

    void MainWindow::NavigateToLogin()
    {
        winrt::Windows::UI::Xaml::Interop::TypeName typeName{ winrt::hstring(winrt::name_of<winrt::epyks_winui::LoginPage>()), winrt::Windows::UI::Xaml::Interop::TypeKind::Custom };
        RootFrame().Navigate(typeName);
    }

    void MainWindow::NavigateToChat()
    {
        winrt::Windows::UI::Xaml::Interop::TypeName typeName{ winrt::hstring(winrt::name_of<winrt::epyks_winui::ChatPage>()), winrt::Windows::UI::Xaml::Interop::TypeKind::Custom };
        RootFrame().Navigate(typeName);
    }
}

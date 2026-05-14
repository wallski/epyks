#pragma once
#include "MainWindow.g.h"
#include "Pages/LoginPage.xaml.h"

namespace winrt::epyks_winui::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void SetupTitleBar();
        void NavigateToLogin();
        void NavigateToChat();

    private:
        winrt::event_token m_activatedRevoker{};
        winrt::event_token m_themeChangedRevoker{};
    };
}

namespace winrt::epyks_winui::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}

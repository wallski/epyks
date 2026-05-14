#pragma once
#include "../pch.h"
#include "LoginPage.g.h"

namespace winrt::epyks_winui::implementation
{
    struct LoginPage : LoginPageT<LoginPage>
    {
        LoginPage();

        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void LoginTabBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RegisterTabBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ActionButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PassBox_KeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);

    private:
        bool m_isRegisterMode = false;
        void SetMode(bool registerMode);
        void AttemptLogin();
        void ShowError(const std::wstring& msg);
        void ShowMessage(const winrt::hstring& msg, bool isError);
        void SetLoading(bool loading);
        void NavigateToChat();
        std::string GetHost();
    };
}

namespace winrt::epyks_winui::factory_implementation
{
    struct LoginPage : LoginPageT<LoginPage, implementation::LoginPage> {};
}

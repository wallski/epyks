#include "pch.h"
#include "LoginPage.xaml.h"
#if __has_include("LoginPage.g.cpp")
#include "LoginPage.g.cpp"
#endif

#include "../Network/EpyksClient.h"
#include "../ViewModels/AppState.h"
#include "ChatPage.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Input;
using namespace Windows::System;

namespace winrt::epyks_winui::implementation
{
    LoginPage::LoginPage()
    {
        InitializeComponent();
    }

    void LoginPage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        FadeInStory().Begin();

        // Wire AppState -> navigate to chat / show errors
        auto& state = ::epyks_winui::GetAppState();
        state.OnStateChanged = [this]()
        {
            auto& s = ::epyks_winui::GetAppState();
            if (s.isLoggedIn)
            {
                SetLoading(false);
                NavigateToChat();
            }
            else if (s.lastOpStatus == "REGISTER_SUCCESS")
            {
                s.lastOpStatus.clear();
                SetLoading(false);
                SetMode(false);
                ShowMessage(L"Account created! Log in to continue.", false /*isError*/);
            }
            else if (!s.lastOpStatus.empty() && s.lastOpStatus.find("failed") != std::string::npos)
            {
                SetLoading(false);
                ShowMessage(winrt::to_hstring(s.lastOpStatus), true /*isError*/);
                s.lastOpStatus.clear();
            }
        };

        // Restore saved credentials
        std::string user, token, inDev, outDev, savedServer;
        bool canAutoLogin = ::epyks_winui::LoadConfig(user, token, inDev, outDev, savedServer);
        
        // Always populate these into AppState if they exist
        ::epyks_winui::GetAppState().audioInDevice = inDev;
        ::epyks_winui::GetAppState().audioOutDevice = outDev;

        // Restore saved server IP (if any)
        if (!savedServer.empty())
            ServerBox().Text(winrt::to_hstring(savedServer));

        if (canAutoLogin)
        {
            // Auto-login via token
            std::string host = GetHost();
            int port = 9001;

            auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            ::epyks_winui::InitAppState(dispatcher);
            ::epyks_winui::GetAppState().username = user;
            ::epyks_winui::GetAppState().serverAddress = host;

            if (::epyks_winui::GetClient().Connect(host.c_str(), port))
            {
                SetLoading(true);
                ::epyks_winui::GetClient().TokenLogin(user, token);
            }
        }
    }

    // Returns the host (IP only, no port) from the hidden ServerBox
    std::string LoginPage::GetHost()
    {
        auto s = winrt::to_string(ServerBox().Text());
        // Strip port if user somehow typed host:port
        auto colon = s.rfind(':');
        if (colon != std::string::npos)
            return s.substr(0, colon);
        return s.empty() ? "26.79.18.226" : s;
    }

    void LoginPage::LoginTabBtn_Click(IInspectable const&, RoutedEventArgs const&)  { SetMode(false); }
    void LoginPage::RegisterTabBtn_Click(IInspectable const&, RoutedEventArgs const&) { SetMode(true); }

    void LoginPage::SetMode(bool registerMode)
    {
        m_isRegisterMode = registerMode;
        ActionButton().Content(box_value(registerMode ? L"Create Account" : L"Log In"));
        SubtitleText().Text(registerMode ? L"Create your account." : L"Welcome back.");

        // Show/hide confirm password
        ConfirmLabel().Visibility(registerMode ? Visibility::Visible : Visibility::Collapsed);
        ConfirmPassBox().Visibility(registerMode ? Visibility::Visible : Visibility::Collapsed);

        // Clear password fields when switching tabs (don't share state)
        PassBox().Password(L"");
        ConfirmPassBox().Password(L"");

        // Swap underline indicator
        LoginTabIndicator().BorderBrush(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(
            255, registerMode ? 0u : 88u, registerMode ? 0u : 101u, registerMode ? 0u : 242u)));
        RegisterTabIndicator().BorderBrush(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(
            255, registerMode ? 88u : 0u, registerMode ? 101u : 0u, registerMode ? 242u : 0u)));

        LoginTabBtn().Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(
            255, registerMode ? 107u : 232u, registerMode ? 114u : 233u, registerMode ? 128u : 234u)));
        RegisterTabBtn().Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(
            255, registerMode ? 232u : 107u, registerMode ? 233u : 114u, registerMode ? 234u : 128u)));

        ErrorText().Visibility(Visibility::Collapsed);
    }

    void LoginPage::PassBox_KeyDown(IInspectable const&, KeyRoutedEventArgs const& e)
    {
        if (e.Key() == VirtualKey::Enter) AttemptLogin();
    }

    void LoginPage::ActionButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        AttemptLogin();
    }

    void LoginPage::AttemptLogin()
    {
        auto user = winrt::to_string(UserBox().Text());
        auto pass = winrt::to_string(PassBox().Password());

        if (user.empty() || pass.empty())
        {
            ShowMessage(L"Please enter a username and password.", true);
            return;
        }

        if (m_isRegisterMode)
        {
            auto confirm = winrt::to_string(ConfirmPassBox().Password());
            if (confirm.empty())
            {
                ShowMessage(L"Please confirm your password.", true);
                return;
            }
            if (pass != confirm)
            {
                ShowMessage(L"Passwords do not match.", true);
                return;
            }
        }

        std::string host = GetHost();
        int port = 9001;

        auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        ::epyks_winui::InitAppState(dispatcher);
        ::epyks_winui::GetAppState().username = user;
        ::epyks_winui::GetAppState().serverAddress = host;
        ::epyks_winui::GetAppState().rememberMe = RememberMeCheck().IsChecked().Value();

        if (!::epyks_winui::GetClient().Connect(host.c_str(), port))
        {
            ShowMessage(L"Could not connect to server. Check your network.", true);
            return;
        }

        SetLoading(true);
        ErrorText().Visibility(Visibility::Collapsed);

        if (m_isRegisterMode)
            ::epyks_winui::GetClient().Register(user, pass);
        else
        {
            ::epyks_winui::GetClient().Login(user, pass);
        }
    }

    void LoginPage::ShowMessage(const winrt::hstring& msg, bool isError)
    {
        ErrorText().Text(msg);
        ErrorText().Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(
            255,
            isError ? 242u : 35u,
            isError ? 63u  : 165u,
            isError ? 67u  : 90u)));
        ErrorText().Visibility(Visibility::Visible);
    }

    // Keep old ShowError signature for any remaining callsites
    void LoginPage::ShowError(const std::wstring& msg)
    {
        ShowMessage(winrt::hstring(msg), true);
    }

    void LoginPage::SetLoading(bool loading)
    {
        LoadingRing().IsActive(loading);
        LoadingRing().Visibility(loading ? Visibility::Visible : Visibility::Collapsed);
        ActionButton().IsEnabled(!loading);
    }

    void LoginPage::NavigateToChat()
    {
        winrt::Windows::UI::Xaml::Interop::TypeName typeName{
            winrt::hstring(winrt::name_of<winrt::epyks_winui::ChatPage>()),
            winrt::Windows::UI::Xaml::Interop::TypeKind::Custom };
        Frame().Navigate(typeName);
    }
}

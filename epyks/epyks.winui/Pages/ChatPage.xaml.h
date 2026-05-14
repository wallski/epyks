#pragma once
#include "../pch.h"
#include "ChatPage.g.h"
#include "../Network/EpyksClient.h"

namespace winrt::epyks_winui::implementation
{
    struct ChatPage : ChatPageT<ChatPage>
    {
        ChatPage();

        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        // Server rail
        void HomeBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget CreateServerBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget BrowseServersBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Sidebar
        winrt::fire_and_forget ServerSettingsBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget MyProfileBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SettingsBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Voice bar
        void MuteBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DeafenBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void VoiceDisconnectBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Chat header
        void MemberListToggle_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Chat
        void MessageInput_KeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void SendBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget AttachBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CancelReplyBtn_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        bool m_memberListVisible = false;
        uint64_t m_replyToId = 0;
        std::string m_replyToUser;
        Microsoft::UI::Xaml::DispatcherTimer m_voiceTimer{ nullptr };

        void RebuildServerRail();
        void RebuildChannelList();
        void RebuildMessageList();
        void RebuildMemberList();
        void RebuildVoiceParticipants();
        void UpdateSpeakingStates();
        void SelectServer(int serverId);
        void SelectChannel(int channelId);
        void SelectDM(const std::string& username);
        void SendMessage();
        void ScrollToBottom();
        void RefreshMyInfo();
        winrt::fire_and_forget ShowProfileFor(std::string username);

        Microsoft::UI::Xaml::UIElement BuildMessageRow(const ::epyks_winui::MsgModel& msg, bool collapsed);
        Microsoft::UI::Xaml::UIElement BuildChannelItem(const ::epyks_winui::ChannelModel& ch);
        Microsoft::UI::Xaml::UIElement BuildFriendItem(const ::epyks_winui::FriendEntry& f);
        Microsoft::UI::Xaml::UIElement BuildServerIcon(int id, const std::string& name);
        Microsoft::UI::Xaml::UIElement BuildMemberItem(const std::string& username);
        Microsoft::UI::Xaml::UIElement BuildVoiceParticipantItem(const std::string& username);
    };
}

namespace winrt::epyks_winui::factory_implementation
{
    struct ChatPage : ChatPageT<ChatPage, implementation::ChatPage> {};
}

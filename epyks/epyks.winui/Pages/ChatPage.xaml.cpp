#include "pch.h"
#include "ChatPage.xaml.h"
#if __has_include("ChatPage.g.cpp")
#include "ChatPage.g.cpp"
#endif

#include "../Network/EpyksClient.h"
#include "../ViewModels/AppState.h"
#include "../AudioClient.h"
#include <shobjidl.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;
using namespace Windows::System;

namespace winrt
{
    namespace epyks_winui
    {
        namespace implementation
        {
    ChatPage::ChatPage()
    {
        InitializeComponent();
    }

    void ChatPage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();
        MyUsernameText().Text(winrt::to_hstring(state.username));

        // Wire state changes to UI refresh
        state.OnStateChanged = [this]()
        {
            RebuildServerRail();
            RebuildChannelList();
            RebuildMessageList();
            RebuildMemberList(); // FIX: Refresh member list on any state change
            RebuildVoiceParticipants();
            RefreshMyInfo();
        };

        if (!m_voiceTimer)
        {
            m_voiceTimer = Microsoft::UI::Xaml::DispatcherTimer();
            m_voiceTimer.Interval(std::chrono::milliseconds(100));
            m_voiceTimer.Tick([this](IInspectable const&, IInspectable const&) {
                UpdateSpeakingStates();
            });
            m_voiceTimer.Start();
        }

        RebuildServerRail();
        RebuildChannelList();
        RebuildMessageList();
    }

    // ---------------------------------------------------------------
    // Rebuild UI from AppState
    // ---------------------------------------------------------------
    void ChatPage::RebuildServerRail()
    {
        auto& state = ::epyks_winui::GetAppState();
        ServerRailPanel().Children().Clear();

        for (auto& pair : state.servers)
        {
            auto id = pair.first;
            auto& srv = pair.second;
            auto icon = BuildServerIcon(id, srv.name);
            ServerRailPanel().Children().Append(icon);
        }
    }

    void ChatPage::RebuildChannelList()
    {
        auto& state = ::epyks_winui::GetAppState();
        ChannelPanel().Children().Clear();

        if (state.currentServerId == -1)
        {
            // DM mode
            SidebarTitle().Text(L"Direct Messages");
            ServerSettingsBtn().Visibility(Visibility::Collapsed);
            ChannelPrefixText().Text(L"@");

            // Friends section header
            {
                auto hdr = TextBlock();
                hdr.Text(L"DIRECT MESSAGES");
                hdr.FontSize(11);
                hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
                hdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
                hdr.Margin(Thickness{ 8, 8, 0, 4 });
                ChannelPanel().Children().Append(hdr);
            }

            for (auto& f : state.friends)
            {
                ChannelPanel().Children().Append(BuildFriendItem(f));
            }

            // Friends manager button (like Discord)
            auto friendsBtn = Button();
            friendsBtn.Content(box_value(L"Friends"));
            friendsBtn.HorizontalAlignment(HorizontalAlignment::Stretch);
            friendsBtn.Margin(Thickness{0, 8, 0, 0});
            friendsBtn.Click([this](auto&&, auto&&) {
                auto& s = ::epyks_winui::GetAppState();
                s.currentDM = "Friends";
                s.currentChannelId = -1;
                s.currentServerId = -1;
                RebuildMessageList();
            });
            ChannelPanel().Children().Append(friendsBtn);
        }
        else
        {
            auto& srv = state.servers[state.currentServerId];
            SidebarTitle().Text(winrt::to_hstring(srv.name));
            ServerSettingsBtn().Visibility(srv.owner == state.username ? Visibility::Visible : Visibility::Collapsed);
            ChannelPrefixText().Text(L"#");

            // Group channels by category
            std::map<std::string, std::vector<::epyks_winui::ChannelModel*>> cats;
            for (auto& ch : srv.channels)
            {
                if (ch.type != 2) // skip category headers
                    cats[ch.category.empty() ? "TEXT CHANNELS" : ch.category].push_back(
                        const_cast<::epyks_winui::ChannelModel*>(&ch));
            }

            for (auto& pair : cats)
            {
                auto& cat = pair.first;
                auto& channels = pair.second;
                // Category header
                auto catHdr = TextBlock();
                catHdr.Text(winrt::to_hstring(cat));
                catHdr.FontSize(11);
                catHdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
                catHdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
                catHdr.Margin(Thickness{ 8, 8, 0, 4 });
                ChannelPanel().Children().Append(catHdr);

                for (auto* ch : channels)
                {
                    ChannelPanel().Children().Append(BuildChannelItem(*ch));
                }
            }
        }
    }

    void ChatPage::RebuildMessageList()
    {
        auto& state = ::epyks_winui::GetAppState();
        MessageList().Items().Clear();

        std::vector<::epyks_winui::MsgModel>* msgs = nullptr;
        std::string chatTitle;

        bool isVoiceChannel = false;
        if (state.currentServerId != -1 && state.currentChannelId != -1)
        {
            auto* srv = state.GetCurrentServer();
            if (srv)
            {
                msgs = state.GetCurrentMessages();
                // Find channel name and type
                for (auto& ch : srv->channels)
                {
                    if (ch.id == state.currentChannelId)
                    {
                        chatTitle = ch.name;
                        isVoiceChannel = (ch.type == 1);
                        break;
                    }
                }
            }
        }
        else if (!state.currentDM.empty())
        {
            if (state.currentDM == "Friends")
            {
                chatTitle = "Friends";
                // Build a special UI for friends manager
                auto friendsGrid = Grid();
                friendsGrid.Margin(Thickness{24});
                
                auto sp = StackPanel();
                sp.Spacing(16);
                
                auto hdr = TextBlock();
                hdr.Text(L"Friends");
                hdr.FontSize(20); hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
                sp.Children().Append(hdr);
                
                // Add friend input
                auto addSp = StackPanel();
                addSp.Orientation(Orientation::Horizontal); addSp.Spacing(8);
                auto addBox = TextBox(); addBox.PlaceholderText(L"Enter a Username#0000"); addBox.Width(300);
                auto addBtn = Button(); addBtn.Content(box_value(L"Send Friend Request"));
                addBtn.Style(Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
                addBtn.Click([addBox](IInspectable const&, RoutedEventArgs const&) {
                    auto u = winrt::to_string(addBox.Text());
                    if (!u.empty()) {
                        ::epyks_winui::GetClient().SendFriendRequest(u);
                        addBox.Text(L"");
                    }
                });
                addSp.Children().Append(addBox);
                addSp.Children().Append(addBtn);
                sp.Children().Append(addSp);

                // Pending requests
                if (!state.friendRequests.empty())
                {
                    auto phdr = TextBlock(); phdr.Text(L"PENDING REQUESTS"); phdr.FontSize(12); phdr.Margin(Thickness{0,16,0,4});
                    sp.Children().Append(phdr);
                    for (auto& req : state.friendRequests)
                    {
                        auto rrow = StackPanel(); rrow.Orientation(Orientation::Horizontal); rrow.Spacing(12);
                        auto rname = TextBlock(); rname.Text(winrt::to_hstring(req)); rname.VerticalAlignment(VerticalAlignment::Center);
                        auto acc = Button(); acc.Content(box_value(L"Accept"));
                        acc.Click([req](IInspectable const&, RoutedEventArgs const&) { ::epyks_winui::GetClient().RespondFriendRequest(req, true); });
                        rrow.Children().Append(rname); rrow.Children().Append(acc);
                        sp.Children().Append(rrow);
                    }
                }

                // Friends List
                auto flhdr = TextBlock(); flhdr.Text(L"ALL FRIENDS"); flhdr.FontSize(12); flhdr.Margin(Thickness{0,16,0,4});
                sp.Children().Append(flhdr);
                for (auto& f : state.friends)
                {
                    auto frow = StackPanel(); frow.Orientation(Orientation::Horizontal); frow.Spacing(12);
                    auto fname = TextBlock(); fname.Text(winrt::to_hstring(f.username)); fname.VerticalAlignment(VerticalAlignment::Center); fname.Width(200);
                    
                    auto btns = StackPanel(); btns.Orientation(Orientation::Horizontal); btns.Spacing(8);
                    
                    auto msgBtn = Button(); msgBtn.Content(box_value(L"Message"));
                    msgBtn.Click([this, user = f.username](auto&&, auto&&) {
                        auto& s = ::epyks_winui::GetAppState();
                        s.currentDM = user;
                        s.currentChannelId = -1;
                        s.currentServerId = -1;
                        RebuildMessageList();
                    });
                    
                    auto unfBtn = Button(); unfBtn.Content(box_value(L"Remove Friend"));
                    unfBtn.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 63, 67)));
                    unfBtn.Click([this, user = f.username](auto&&, auto&&) {
                        ::epyks_winui::GetClient().Unfriend(user);
                    });
                    
                    btns.Children().Append(msgBtn); btns.Children().Append(unfBtn);
                    frow.Children().Append(fname); frow.Children().Append(btns);
                    sp.Children().Append(frow);
                }

                friendsGrid.Children().Append(sp);
                MessageList().Items().Append(friendsGrid);
            }
            else
            {
                auto it = state.dmChats.find(state.currentDM);
                if (it != state.dmChats.end())
                    msgs = &it->second.messages;
                chatTitle = state.currentDM;
            }
        }

        ChannelHeaderText().Text(winrt::to_hstring(chatTitle));
        
        if (isVoiceChannel)
        {
            VoiceScreenPanel().Visibility(Visibility::Visible);
            MessageList().Visibility(Visibility::Collapsed);
            MessageInputPanel().Visibility(Visibility::Collapsed);
            EmptyChatPlaceholder().Visibility(Visibility::Collapsed);
            RebuildVoiceParticipants();
            return;
        }
        else
        {
            VoiceScreenPanel().Visibility(Visibility::Collapsed);
            MessageList().Visibility(Visibility::Visible);
            MessageInputPanel().Visibility(Visibility::Visible);
            EmptyChatPlaceholder().Visibility((!msgs || msgs->empty()) ? Visibility::Visible : Visibility::Collapsed);
        }

        if (!msgs || msgs->empty()) return;

        std::string prevSender;
        for (size_t i = 0; i < msgs->size(); i++)
        {
            auto& m = (*msgs)[i];
            bool collapsed = (m.sender == prevSender);
            MessageList().Items().Append(BuildMessageRow(m, collapsed));
            prevSender = m.sender;
        }

        ScrollToBottom();
    }

    void ChatPage::RebuildMemberList()
    {
        auto& state = ::epyks_winui::GetAppState();
        MemberListPanel().Children().Clear();
        if (state.currentServerId == -1) return;

        auto* srv = state.GetCurrentServer();
        if (!srv || srv->members.empty()) return;

        // Section: Online
        bool addedOnlineHeader = false;
        bool addedOfflineHeader = false;

        for (auto& member : srv->members)
        {
            if (member.online)
            {
                if (!addedOnlineHeader)
                {
                    auto hdr = TextBlock();
                    hdr.Text(L"ONLINE");
                    hdr.FontSize(11);
                    hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
                    hdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
                    hdr.Margin(Thickness{8, 8, 8, 4});
                    MemberListPanel().Children().Append(hdr);
                    addedOnlineHeader = true;
                }
                MemberListPanel().Children().Append(BuildMemberItem(member.username));
            }
        }

        for (auto& member : srv->members)
        {
            if (!member.online)
            {
                if (!addedOfflineHeader)
                {
                    auto hdr = TextBlock();
                    hdr.Text(L"OFFLINE");
                    hdr.FontSize(11);
                    hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
                    hdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 100, 102, 110)));
                    hdr.Margin(Thickness{8, 12, 8, 4});
                    MemberListPanel().Children().Append(hdr);
                    addedOfflineHeader = true;
                }
                MemberListPanel().Children().Append(BuildMemberItem(member.username));
            }
        }
    }

    void ChatPage::RebuildVoiceParticipants()
    {
        auto& state = ::epyks_winui::GetAppState();
        VoiceParticipantsGrid().Items().Clear();
        if (state.currentServerId == -1 || state.currentChannelId == -1) return;

        auto* srv = state.GetCurrentServer();
        if (!srv) return;

        // Collect participants in this voice channel (members with voice_channel_id == currentChannelId)
        // Always show ourselves first
        VoiceParticipantsGrid().Items().Append(BuildVoiceParticipantItem(state.username));

        for (auto& member : srv->members)
        {
            if (member.username != state.username &&
                member.voice_channel_id == state.currentChannelId)
            {
                VoiceParticipantsGrid().Items().Append(BuildVoiceParticipantItem(member.username));
            }
        }
    }

    void ChatPage::UpdateSpeakingStates()
    {
        auto& state = ::epyks_winui::GetAppState();
        if (!state.inVoice) return;
        
        for (auto item : VoiceParticipantsGrid().Items())
        {
            if (auto tile = item.try_as<winrt::Microsoft::UI::Xaml::Controls::Border>())
            {
                if (auto panel = tile.Child().try_as<winrt::Microsoft::UI::Xaml::Controls::StackPanel>())
                {
                    if (panel.Children().Size() >= 2)
                    {
                        auto speakBorder = panel.Children().GetAt(0).try_as<winrt::Microsoft::UI::Xaml::Controls::Border>();
                        auto nameLabel = panel.Children().GetAt(1).try_as<winrt::Microsoft::UI::Xaml::Controls::TextBlock>();
                        std::string username = winrt::to_string(nameLabel.Text());
                        
                        bool isTalking = false;
                        if (username == state.username)
                            isTalking = ::epyks_winui::GetAudioClient().IsSpeaking();
                        else
                        {
                            auto it = state.voiceActivity.find(username);
                            if (it != state.voiceActivity.end()) {
                                isTalking = (GetTickCount64() - it->second) < 500;
                            }
                        }

                        speakBorder.BorderThickness(Thickness{ isTalking ? 4.0 : 0.0 });
                        speakBorder.BorderBrush(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(isTalking ? 255 : 0, 35, 165, 90)));
                    }
                }
            }
        }
    }

    void ChatPage::RefreshMyInfo()
    {
        auto& state = ::epyks_winui::GetAppState();
        MyUsernameText().Text(winrt::to_hstring(state.username));
        MyAvatarEllipse().Fill(::epyks_winui::GetAvatarBrush(state.username));

        if (state.inVoice)
        {
            MuteBtn().Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, state.isMuted ? 242u : 148u, state.isMuted ? 63u : 155u, state.isMuted ? 67u : 164u)));
            DeafenBtn().Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, state.isDeafened ? 242u : 148u, state.isDeafened ? 63u : 155u, state.isDeafened ? 67u : 164u)));
            
            MuteIcon().Glyph(state.isMuted ? L"\xEC71" : L"\xE720");
            DeafenIcon().Glyph(state.isDeafened ? L"\xEF2F" : L"\xE7F6");
            
            VoiceScreenMuteIcon().Glyph(state.isMuted ? L"\xEC71" : L"\xE720");
            VoiceScreenDeafenIcon().Glyph(state.isDeafened ? L"\xEF2F" : L"\xE7F6");
            
            AccountBarDisconnectBtn().Visibility(Visibility::Visible);
        }
        else
        {
            AccountBarDisconnectBtn().Visibility(Visibility::Collapsed);
            MuteIcon().Glyph(L"\xE720");
            DeafenIcon().Glyph(L"\xE7F6");
            VoiceScreenMuteIcon().Glyph(L"\xE720");
            VoiceScreenDeafenIcon().Glyph(L"\xE7F6");
        }
    }

    // ---------------------------------------------------------------
    // UI element builders
    // ---------------------------------------------------------------
    UIElement ChatPage::BuildServerIcon(int id, const std::string& name)
    {
        auto& state = ::epyks_winui::GetAppState();
        bool active = (id == state.currentServerId);

        auto btn = Button();
        btn.Width(48); btn.Height(48); btn.Padding(Thickness{0});
        btn.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{ active ? 16.0 : 24.0, active ? 16.0 : 24.0, active ? 16.0 : 24.0, active ? 16.0 : 24.0 });
        btn.BorderThickness(Thickness{0});

        auto label = name.empty() ? "?" : name.substr(0, 2);
        auto tb = TextBlock();
        tb.Text(winrt::to_hstring(label));
        tb.FontSize(15);
        tb.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        btn.Content(tb);

        SolidColorBrush bg(active
            ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 88, 101, 242)
            : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 54, 57, 63));
        btn.Background(bg);

        btn.Click([this, id](IInspectable const&, RoutedEventArgs const&) { SelectServer(id); });
        btn.Margin(Thickness{0, 4, 0, 0});

        ToolTipService::SetToolTip(btn, box_value(winrt::to_hstring(name + " (ID: " + std::to_string(id) + ")")));
        return btn;
    }

    UIElement ChatPage::BuildChannelItem(const ::epyks_winui::ChannelModel& ch)
    {
        auto& state = ::epyks_winui::GetAppState();
        bool active = (ch.id == state.currentChannelId);

        auto btn = Button();
        btn.HorizontalAlignment(HorizontalAlignment::Stretch);
        btn.HorizontalContentAlignment(HorizontalAlignment::Left);
        btn.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{4,4,4,4});
        btn.BorderThickness(Thickness{0});
        btn.Padding(Thickness{8, 4, 8, 4});

        SolidColorBrush bg(active
            ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 64, 66, 73)
            : winrt::Microsoft::UI::ColorHelper::FromArgb(0, 0, 0, 0));
        btn.Background(bg);

        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(6.0);

        auto prefix = TextBlock();
        prefix.Text(ch.type == 1 ? L"v" : L"#");
        prefix.FontSize(ch.type == 1 ? 11 : 13);
        prefix.FontStyle(ch.type == 1 ? winrt::Windows::UI::Text::FontStyle::Italic : winrt::Windows::UI::Text::FontStyle::Normal);
        prefix.FontWeight(ch.type == 1 ? winrt::Microsoft::UI::Text::FontWeights::Bold() : winrt::Microsoft::UI::Text::FontWeights::Normal());
        prefix.VerticalAlignment(VerticalAlignment::Center);
        prefix.Foreground(SolidColorBrush(
            winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));

        auto nameText = TextBlock();
        nameText.Text(winrt::to_hstring(ch.name));
        nameText.FontSize(14);
        nameText.Foreground(SolidColorBrush(
            active ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 243, 245)
                   : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));

        row.Children().Append(prefix);
        row.Children().Append(nameText);
        btn.Content(row);

        int chId = ch.id;
        int chType = ch.type;
        btn.Click([this, chId, chType](IInspectable const&, RoutedEventArgs const&) {
            if (chType == 1)
            {
                // Voice channel — join voice
                auto& s = ::epyks_winui::GetAppState();
                if (s.voiceChannelId != chId)
                {
                    ::epyks_winui::GetClient().JoinVoice(s.currentServerId, chId);
                    ::epyks_winui::GetAudioClient().StartVoice();
                    s.inVoice = true;
                    s.voiceServerId = s.currentServerId;
                    s.voiceChannelId = chId;
                    RefreshMyInfo();
                }
                SelectChannel(chId);
            }
            else
            {
                SelectChannel(chId);
            }
        });

        return btn;
    }

    UIElement ChatPage::BuildFriendItem(const ::epyks_winui::FriendEntry& f)
    {
        auto& state = ::epyks_winui::GetAppState();
        bool active = (state.currentDM == f.username);

        auto btn = Button();
        btn.HorizontalAlignment(HorizontalAlignment::Stretch);
        btn.HorizontalContentAlignment(HorizontalAlignment::Left);
        btn.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{4,4,4,4});
        btn.BorderThickness(Thickness{0});
        btn.Padding(Thickness{8, 4, 8, 4});

        SolidColorBrush bg(active
            ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 64, 66, 73)
            : winrt::Microsoft::UI::ColorHelper::FromArgb(0, 0, 0, 0));
        btn.Background(bg);

        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(8.0);

        // Avatar circle
        auto ell = winrt::Microsoft::UI::Xaml::Shapes::Ellipse();
        ell.Width(28); ell.Height(28);
        ell.Fill(::epyks_winui::GetAvatarBrush(f.username));

        auto nameText = TextBlock();
        nameText.Text(winrt::to_hstring(f.username));
        nameText.FontSize(14);
        nameText.VerticalAlignment(VerticalAlignment::Center);
        nameText.Foreground(SolidColorBrush(
            f.hasUnread ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 255, 255, 255)
                        : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));

        row.Children().Append(ell);
        row.Children().Append(nameText);
        btn.Content(row);

        std::string user = f.username;
        btn.Click([this, user](IInspectable const&, RoutedEventArgs const&) { SelectDM(user); });
        return btn;
    }

    UIElement ChatPage::BuildMessageRow(const ::epyks_winui::MsgModel& msg, bool collapsed)
    {
        auto outer = Grid();
        outer.Padding(Thickness{16, collapsed ? 2.0 : 8.0, 16, 2});

        auto col0 = ColumnDefinition();
        col0.Width(GridLengthHelper::FromPixels(40));
        auto col1 = ColumnDefinition();
        col1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        outer.ColumnDefinitions().Append(col0);
        outer.ColumnDefinitions().Append(col1);

        if (!collapsed)
        {
            // Clickable avatar button showing initial letter
            auto avatarBtn = Button();
            avatarBtn.Width(36); avatarBtn.Height(36);
            avatarBtn.Padding(Thickness{0});
            avatarBtn.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{18,18,18,18});
            avatarBtn.BorderThickness(Thickness{0});
            avatarBtn.VerticalAlignment(VerticalAlignment::Top);
            avatarBtn.Background(::epyks_winui::GetAvatarBrush(msg.sender));
            
            // Only add initials if we don't have a cached profile picture
            std::string cachePath = ::epyks_winui::GetCachePath(msg.sender + ".png");
            if (!std::filesystem::exists(cachePath))
            {
                auto initials = TextBlock();
                initials.Text(winrt::to_hstring(msg.sender.empty() ? "?" : msg.sender.substr(0, 1)));
                initials.FontSize(14); initials.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
                initials.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,255,255,255)));
                initials.HorizontalAlignment(HorizontalAlignment::Center);
                initials.VerticalAlignment(VerticalAlignment::Center);
                avatarBtn.Content(initials);
            }
            Grid::SetColumn(avatarBtn, 0);
            std::string senderForAvatar = msg.sender;
            avatarBtn.Click([this, senderForAvatar](auto&&, auto&&) {
                ShowProfileFor(senderForAvatar);
            });
            outer.Children().Append(avatarBtn);
        }

        // Content column
        auto content = StackPanel();
        content.Spacing(2.0);
        Grid::SetColumn(content, 1);

        if (!collapsed)
        {
            // Author row
            auto authorRow = StackPanel();
            authorRow.Orientation(Orientation::Horizontal);
            authorRow.Spacing(8.0);

            auto authorName = TextBlock();
            authorName.Text(winrt::to_hstring(msg.sender));
            authorName.FontSize(14);
            authorName.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            authorName.Foreground(SolidColorBrush(
                winrt::Microsoft::UI::ColorHelper::FromArgb(255, 255, 255, 255)));
            // Click name to show profile
            std::string senderName = msg.sender;
            authorName.PointerPressed([this, senderName](auto&&, auto&&) {
                ShowProfileFor(senderName);
            });
            authorRow.Children().Append(authorName);

            content.Children().Append(authorRow);
        }

        // Reply reference
        if (msg.replyToId != 0)
        {
            auto replyBar = TextBlock();
            replyBar.Text(L"↩ Reply");
            replyBar.FontSize(12);
            replyBar.Foreground(SolidColorBrush(
                winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
            content.Children().Append(replyBar);
        }

        // Message body
        if (msg.content.find("server-media://") == 0)
        {
            std::string fname = msg.content.substr(15);
            std::string localPath = ::epyks_winui::GetCachePath(fname);
            
            auto img = winrt::Microsoft::UI::Xaml::Controls::Image();
            auto bmp = winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage();
            // Use file:/// prefix for local files
            std::string uriStr = "file:///" + localPath;
            std::replace(uriStr.begin(), uriStr.end(), '\\', '/');
            bmp.UriSource(winrt::Windows::Foundation::Uri(winrt::to_hstring(uriStr)));
            img.Source(bmp);
            img.MaxWidth(400);
            img.HorizontalAlignment(HorizontalAlignment::Left);
            img.Stretch(Stretch::Uniform);
            img.Margin(Thickness{0, 4, 0, 4});
            content.Children().Append(img);
        }
        else
        {
            auto msgText = TextBlock();
            msgText.Text(winrt::to_hstring(msg.content));
            msgText.FontSize(14);
            msgText.TextWrapping(TextWrapping::WrapWholeWords);
            msgText.Foreground(SolidColorBrush(
                winrt::Microsoft::UI::ColorHelper::FromArgb(255, 220, 221, 222)));
            content.Children().Append(msgText);
        }

        outer.Children().Append(content);
        return outer;
    }

    UIElement ChatPage::BuildMemberItem(const std::string& username)
    {
        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(8.0);
        row.Padding(Thickness{4, 3, 4, 3});

        auto ell = winrt::Microsoft::UI::Xaml::Shapes::Ellipse();
        ell.Width(28); ell.Height(28);
        ell.Fill(::epyks_winui::GetAvatarBrush(username));

        auto tb = TextBlock();
        tb.Text(winrt::to_hstring(username));
        tb.FontSize(13);
        tb.VerticalAlignment(VerticalAlignment::Center);
        tb.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));

        row.Children().Append(ell);
        row.Children().Append(tb);
        return row;
    }

    UIElement ChatPage::BuildVoiceParticipantItem(const std::string& username)
    {
        // Discord style dark tile
        auto tile = Border();
        tile.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 30, 31, 34)));
        tile.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{12,12,12,12});
        tile.Margin(Thickness{8});
        tile.Width(200);
        tile.Height(160);

        auto panel = StackPanel();
        panel.HorizontalAlignment(HorizontalAlignment::Center);
        panel.VerticalAlignment(VerticalAlignment::Center);
        panel.Spacing(12.0);

        // Speaking border wrapping the avatar
        auto& state = ::epyks_winui::GetAppState();
        bool isSelf = (username == state.username);
        // Check if speaking: for self use AudioClient, for others check server member talking flag
        bool isSpeaking = false;
        if (isSelf)
        {
            // We'd check AudioClient::IsSpeaking() here
        }
        else if (state.currentServerId != -1)
        {
            auto* srv = state.GetCurrentServer();
            if (srv)
            {
                for (auto& m : srv->members)
                    if (m.username == username) { isSpeaking = m.is_talking; break; }
            }
        }

        auto speakBorder = Border();
        speakBorder.Width(88);
        speakBorder.Height(88);
        speakBorder.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{44,44,44,44});
        speakBorder.BorderThickness(Thickness{isSpeaking ? 3.0 : 0.0});
        speakBorder.BorderBrush(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Microsoft::UI::ColorHelper::FromArgb(255, 35, 165, 90)));
        speakBorder.HorizontalAlignment(HorizontalAlignment::Center);
        speakBorder.Padding(Thickness{3});

        // Avatar circle
        auto ell = winrt::Microsoft::UI::Xaml::Shapes::Ellipse();
        ell.Width(80);
        ell.Height(80);
        ell.Fill(::epyks_winui::GetAvatarBrush(username));
        speakBorder.Child(ell);
        panel.Children().Append(speakBorder);

        // Username label
        auto nameLabel = TextBlock();
        nameLabel.Text(winrt::to_hstring(username));
        nameLabel.FontSize(14);
        nameLabel.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        nameLabel.HorizontalAlignment(HorizontalAlignment::Center);
        nameLabel.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 243, 245)));
        panel.Children().Append(nameLabel);
        
        tile.Child(panel);

        // Volume flyout on right-click (only for other users)
        if (!isSelf)
        {
            // Use a Flyout (not MenuFlyout) so we can put a Slider inside it
            auto flyout = Flyout();
            auto flyoutPanel = StackPanel();
            flyoutPanel.Padding(Thickness{8});
            flyoutPanel.Spacing(8);
            flyoutPanel.Width(220);

            auto volLabel = TextBlock();
            volLabel.Text(L"User Volume");
            volLabel.FontSize(12);
            volLabel.Foreground(SolidColorBrush(
                winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
            flyoutPanel.Children().Append(volLabel);

            auto volSlider = Slider();
            volSlider.Minimum(0); volSlider.Maximum(200); volSlider.Value(100);
            volSlider.HorizontalAlignment(HorizontalAlignment::Stretch);
            std::string capturedUser = username;
            volSlider.ValueChanged([capturedUser](auto&&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e) {
                // AudioClient::SetUserVolume(capturedUser, e.NewValue() / 100.0f);
                (void)capturedUser; (void)e;
            });
            flyoutPanel.Children().Append(volSlider);
            flyout.Content(flyoutPanel);

            // Attach flyout and trigger on right-click
            tile.ContextFlyout(flyout);
        }

        return tile;
    }

    // ---------------------------------------------------------------
    // Navigation
    // ---------------------------------------------------------------
    void ChatPage::SelectServer(int serverId)
    {
        auto& state = ::epyks_winui::GetAppState();
        state.currentServerId = serverId;
        state.currentChannelId = -1;
        state.currentDM.clear();
        ::epyks_winui::GetClient().RequestChannelList(serverId);
        ::epyks_winui::GetClient().RequestMemberList(serverId);
        RebuildServerRail();
        RebuildChannelList();
        RebuildMessageList();
    }

    void ChatPage::SelectChannel(int channelId)
    {
        auto& state = ::epyks_winui::GetAppState();
        state.currentChannelId = channelId;

        // Clear and re-request history
        auto* srv = state.GetCurrentServer();
        if (srv) srv->channelMessages[channelId].clear();

        ::epyks_winui::GetClient().RequestServerHistory(state.currentServerId, channelId);
        RebuildChannelList();
        RebuildMessageList();
    }

    void ChatPage::SelectDM(const std::string& username)
    {
        auto& state = ::epyks_winui::GetAppState();
        state.currentServerId = -1;
        state.currentChannelId = -1;
        state.currentDM = username;
        RebuildChannelList();
        RebuildMessageList();
    }

    // ---------------------------------------------------------------
    // Send
    // ---------------------------------------------------------------
    void ChatPage::SendMessage()
    {
        auto& state = ::epyks_winui::GetAppState();
        auto text = winrt::to_string(MessageInput().Text());
        if (text.empty()) return;

        if (state.currentServerId != -1 && state.currentChannelId != -1)
        {
            ::epyks_winui::GetClient().SendServerMessage(state.currentServerId, state.currentChannelId, text, m_replyToId);
        }
        else if (!state.currentDM.empty())
        {
            ::epyks_winui::GetClient().SendPrivateMessage(state.currentDM, text, m_replyToId);
        }

        MessageInput().Text(L"");
        m_replyToId = 0;
        m_replyToUser.clear();
        ReplyPreviewBar().Visibility(Visibility::Collapsed);
    }

    void ChatPage::ScrollToBottom()
    {
        // Scroll to end of ListView
        auto sv = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(MessageList(), 0).try_as<ScrollViewer>();
        if (!sv)
        {
            if (auto border = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(MessageList(), 0).try_as<Border>())
            {
                sv = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(border, 0).try_as<ScrollViewer>();
            }
        }
        if (sv) {
            MessageList().UpdateLayout();
            sv.ScrollToVerticalOffset(sv.ScrollableHeight());
        }
    }

    // ---------------------------------------------------------------
    // Button handlers
    // ---------------------------------------------------------------
    void ChatPage::HomeBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();
        state.currentServerId = -1;
        state.currentChannelId = -1;
        RebuildServerRail();
        RebuildChannelList();
        RebuildMessageList();
    }

    winrt::fire_and_forget ChatPage::CreateServerBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ContentDialog dlg;
        dlg.XamlRoot(this->XamlRoot());
        dlg.Title(box_value(L"Create Server"));
        dlg.PrimaryButtonText(L"Create");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(ContentDialogButton::Primary);

        auto panel = StackPanel();
        panel.Spacing(12.0);

        auto nameLabel = TextBlock();
        nameLabel.Text(L"Server Name");
        nameLabel.FontSize(13);
        nameLabel.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,148,155,164)));
        panel.Children().Append(nameLabel);

        auto nameBox = TextBox();
        nameBox.PlaceholderText(L"My Awesome Server");
        panel.Children().Append(nameBox);

        auto passLabel = TextBlock();
        passLabel.Text(L"Password (optional)");
        passLabel.FontSize(13);
        passLabel.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,148,155,164)));
        panel.Children().Append(passLabel);

        auto passBox = PasswordBox();
        passBox.PlaceholderText(L"Leave blank for public server");
        panel.Children().Append(passBox);

        dlg.Content(panel);

        auto result = co_await dlg.ShowAsync();
        if (result == ContentDialogResult::Primary)
        {
            auto name = winrt::to_string(nameBox.Text());
            auto pass = winrt::to_string(passBox.Password());
            if (!name.empty())
                ::epyks_winui::GetClient().SendCreateServer(name, pass);
        }
    }

    winrt::fire_and_forget ChatPage::BrowseServersBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // Request available servers first
        ::epyks_winui::GetClient().SendListServers();

        ContentDialog dlg;
        dlg.XamlRoot(this->XamlRoot());
        dlg.Title(box_value(L"Browse Servers"));
        dlg.CloseButtonText(L"Close");

        auto panel = StackPanel();
        panel.Spacing(8.0);
        panel.MinWidth(340);

        auto note = TextBlock();
        note.Text(L"Enter a server name or ID to join:");
        note.FontSize(13);
        note.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,148,155,164)));
        panel.Children().Append(note);

        auto idBox = TextBox();
        idBox.PlaceholderText(L"Server ID");
        panel.Children().Append(idBox);

        auto passBox = PasswordBox();
        passBox.PlaceholderText(L"Password (if required)");
        panel.Children().Append(passBox);

        auto joinBtn = Button();
        joinBtn.Content(box_value(L"Join Server"));
        joinBtn.Style(Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
        joinBtn.HorizontalAlignment(HorizontalAlignment::Stretch);
        joinBtn.Click([idBox, passBox, &dlg](auto&&, auto&&) {
            auto input = winrt::to_string(idBox.Text());
            auto pass = winrt::to_string(passBox.Password());
            if (input.empty()) return;

            try {
                // If it's a numeric ID, join directly
                int id = std::stoi(input);
                ::epyks_winui::GetClient().SendJoinServer(id, pass);
                dlg.Hide();
            } catch (...) {
                // If not numeric, we could search for the name...
                // But the protocol requires an ID. 
                // We will add a note that IDs are required for now or implement search.
            }
        });
        panel.Children().Append(joinBtn);

        dlg.Content(panel);
        co_await dlg.ShowAsync();
    }

    winrt::fire_and_forget ChatPage::ServerSettingsBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();
        if (state.currentServerId == -1) co_return;
        auto& srv = state.servers[state.currentServerId];
        int sid = state.currentServerId;

        ContentDialog dlg;
        dlg.XamlRoot(this->XamlRoot());
        dlg.Title(box_value(winrt::to_hstring(srv.name + " — Settings")));
        dlg.PrimaryButtonText(L"Save");
        dlg.CloseButtonText(L"Close");
        dlg.DefaultButton(ContentDialogButton::Primary);

        auto scroll = ScrollViewer();
        scroll.MaxHeight(520);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);

        auto panel = StackPanel();
        panel.Spacing(10.0);
        panel.MinWidth(380);
        panel.Padding(Thickness{0, 4, 16, 4});
        scroll.Content(panel);

        auto mkLabel = [](const wchar_t* txt) {
            auto tb = TextBlock();
            tb.Text(txt);
            tb.FontSize(11); tb.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            tb.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
            tb.Margin(Thickness{0, 8, 0, 4});
            return tb;
        };
        auto mkSep = []() {
            auto b = Border();
            b.Height(1); b.Background(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 63, 65, 71)));
            b.Margin(Thickness{0, 4, 0, 4});
            return b;
        };

        // ---- Rename server ----
        panel.Children().Append(mkLabel(L"SERVER NAME"));
        auto nameBox = TextBox();
        nameBox.Text(winrt::to_hstring(srv.name));
        panel.Children().Append(nameBox);

        // ---- Server ID ----
        panel.Children().Append(mkLabel(L"SERVER ID"));
        auto idRow = StackPanel();
        idRow.Orientation(Orientation::Horizontal);
        idRow.Spacing(12.0);

        auto idBox = TextBox();
        idBox.Text(winrt::to_hstring(std::to_string(sid)));
        idBox.IsReadOnly(true);
        idBox.Width(100);
        idRow.Children().Append(idBox);

        auto copyBtn = Button();
        copyBtn.Content(box_value(L"Copy ID"));
        copyBtn.Click([sid](IInspectable const&, RoutedEventArgs const&) {
            auto data = winrt::Windows::ApplicationModel::DataTransfer::DataPackage();
            data.SetText(winrt::to_hstring(std::to_string(sid)));
            winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
        });
        idRow.Children().Append(copyBtn);
        panel.Children().Append(idRow);

        panel.Children().Append(mkSep());

        // ---- Channel list ----
        panel.Children().Append(mkLabel(L"CHANNELS"));
        for (auto& ch : srv.channels)
        {
            auto row = StackPanel();
            row.Orientation(Orientation::Horizontal);
            row.Spacing(8.0);

            auto icon = TextBlock();
            icon.Text(ch.type == 1 ? L"V" : L"#");
            icon.FontSize(12);
            icon.VerticalAlignment(VerticalAlignment::Center);
            icon.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));

            auto name = TextBlock();
            name.Text(winrt::to_hstring(ch.name));
            name.FontSize(14);
            name.VerticalAlignment(VerticalAlignment::Center);
            name.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 220, 221, 222)));

            auto cat = TextBlock();
            if (!ch.category.empty())
            {
                cat.Text(winrt::to_hstring(" [" + ch.category + "]"));
                cat.FontSize(11);
                cat.VerticalAlignment(VerticalAlignment::Center);
                cat.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
            }

            row.Children().Append(icon);
            row.Children().Append(name);
            if (!ch.category.empty()) row.Children().Append(cat);

            auto delBtn = Button();
            delBtn.Content(box_value(L"X"));
            delBtn.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 63, 67)));
            delBtn.Background(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            delBtn.BorderThickness(Thickness{0});
            delBtn.Padding(Thickness{4});
            delBtn.HorizontalAlignment(HorizontalAlignment::Right);
            
            int chId = ch.id;
            delBtn.Click([sid, chId](IInspectable const&, RoutedEventArgs const&) {
                ::epyks_winui::GetClient().DeleteChannel(sid, chId);
            });

            auto spacer = Border(); spacer.Width(4);
            row.Children().Append(spacer);
            row.Children().Append(delBtn);

            panel.Children().Append(row);
        }

        panel.Children().Append(mkSep());

        // ---- Add channel ----
        panel.Children().Append(mkLabel(L"ADD CHANNEL"));

        auto chNameBox = TextBox();
        chNameBox.PlaceholderText(L"channel-name");
        panel.Children().Append(chNameBox);

        // Get unique categories
        std::set<std::string> categories;
        for (auto& c : srv.channels) if (!c.category.empty()) categories.insert(c.category);

        auto catCombo = ComboBox();
        catCombo.Items().Append(box_value(L"No Category"));
        for (auto& c : categories) catCombo.Items().Append(box_value(winrt::to_hstring(c)));
        catCombo.Items().Append(box_value(L"+ New Category..."));
        catCombo.SelectedIndex(0);
        catCombo.HorizontalAlignment(HorizontalAlignment::Stretch);
        catCombo.Margin(Thickness{0, 4, 0, 4});
        panel.Children().Append(catCombo);

        auto newCatBox = TextBox();
        newCatBox.PlaceholderText(L"New Category Name");
        newCatBox.Visibility(Visibility::Collapsed);
        panel.Children().Append(newCatBox);

        catCombo.SelectionChanged([newCatBox](auto&& sender, auto&&) {
            auto cb = sender.as<ComboBox>();
            if (cb.SelectedIndex() == (int)cb.Items().Size() - 1)
                newCatBox.Visibility(Visibility::Visible);
            else
                newCatBox.Visibility(Visibility::Collapsed);
        });

        auto chTypeCombo = ComboBox();
        chTypeCombo.Items().Append(box_value(L"Text Channel"));
        chTypeCombo.Items().Append(box_value(L"Voice Channel"));
        chTypeCombo.SelectedIndex(0);
        chTypeCombo.HorizontalAlignment(HorizontalAlignment::Stretch);
        panel.Children().Append(chTypeCombo);

        auto addChBtn = Button();
        addChBtn.Content(box_value(L"Add Channel"));
        addChBtn.HorizontalAlignment(HorizontalAlignment::Stretch);
        addChBtn.Style(Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
        addChBtn.Click([sid, chNameBox, catCombo, newCatBox, chTypeCombo](IInspectable const&, RoutedEventArgs const&) {
            auto n = winrt::to_string(chNameBox.Text());
            std::string cat = "";
            int sel = catCombo.SelectedIndex();
            if (sel > 0 && sel < (int)catCombo.Items().Size() - 1)
                cat = winrt::to_string(winrt::unbox_value<winrt::hstring>(catCombo.Items().GetAt(sel)));
            else if (sel == (int)catCombo.Items().Size() - 1)
                cat = winrt::to_string(newCatBox.Text());

            if (!n.empty()) {
                int t = chTypeCombo.SelectedIndex();
                ::epyks_winui::GetClient().CreateChannel(sid, n, t, cat);
                chNameBox.Text(L"");
                newCatBox.Text(L"");
                catCombo.SelectedIndex(0);
            }
        });
        panel.Children().Append(addChBtn);

        panel.Children().Append(mkSep());

        // ---- Manage Categories ----
        panel.Children().Append(mkLabel(L"MANAGE CATEGORIES"));
        if (categories.empty())
        {
            auto none = TextBlock();
            none.Text(L"No categories defined.");
            none.FontSize(13); none.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
            none.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 100, 102, 110)));
            panel.Children().Append(none);
        }
        for (auto& c : categories)
        {
            auto crow = StackPanel();
            crow.Orientation(Orientation::Horizontal);
            crow.Spacing(8.0);

            auto cname = TextBlock();
            cname.Text(winrt::to_hstring(c));
            cname.FontSize(14);
            cname.VerticalAlignment(VerticalAlignment::Center);
            cname.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 220, 221, 222)));
            cname.Width(200);

            auto cdel = Button();
            cdel.Content(box_value(L"Delete Category"));
            cdel.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 63, 67)));
            
            std::string catToDelete = c;
            cdel.Click([sid, catToDelete, &srv](IInspectable const&, RoutedEventArgs const&) {
                // To delete a category, we delete all channels in it
                for (auto& ch : srv.channels) {
                    if (ch.category == catToDelete) {
                        ::epyks_winui::GetClient().DeleteChannel(sid, ch.id);
                    }
                }
            });

            crow.Children().Append(cname);
            crow.Children().Append(cdel);
            panel.Children().Append(crow);
        }

        panel.Children().Append(mkSep());

        // ---- Members ----
        panel.Children().Append(mkLabel(L"MEMBERS"));
        // List members from server (friends who are in this server — approximate from friend list)
        auto& allFriends = state.friends;
        bool anyMember = false;
        for (auto& f : allFriends)
        {
            auto row = StackPanel();
            row.Orientation(Orientation::Horizontal);
            row.Spacing(8.0);

            auto name = TextBlock();
            name.Text(winrt::to_hstring(f.username));
            name.FontSize(14);
            name.VerticalAlignment(VerticalAlignment::Center);
            name.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 220, 221, 222)));
            name.Width(200);

            auto kickBtn = Button();
            kickBtn.Content(box_value(L"Kick"));
            kickBtn.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 63, 67)));
            kickBtn.FontSize(11);
            kickBtn.Padding(Thickness{6, 2, 6, 2});
            kickBtn.Click([this, target = f.username, sid](auto&&, auto&&) {
                ::epyks_winui::GetClient().KickUser(sid, target);
            });

            row.Children().Append(name);
            row.Children().Append(kickBtn);
            panel.Children().Append(row);
            anyMember = true;
        }
        if (!anyMember)
        {
            auto none = TextBlock();
            none.Text(L"No friends in member list yet.");
            none.FontSize(13);
            none.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
            panel.Children().Append(none);
        }

        panel.Children().Append(mkSep());

        // ---- Danger zone ----
        panel.Children().Append(mkLabel(L"DANGER ZONE"));
        auto leaveBtn = Button();
        leaveBtn.Content(box_value(srv.owner == state.username ? L"Delete Server" : L"Leave Server"));
        leaveBtn.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 63, 67)));
        leaveBtn.HorizontalAlignment(HorizontalAlignment::Stretch);
        leaveBtn.Click([this, sid, isOwner = (srv.owner == state.username)](IInspectable const&, RoutedEventArgs const&) {
            if (isOwner)
                ::epyks_winui::GetClient().SendLeaveServer(sid); // Server handles delete if owner leaves
            else
                ::epyks_winui::GetClient().SendLeaveServer(sid);
            
            auto& s = ::epyks_winui::GetAppState();
            s.servers.erase(sid);
            s.currentServerId = -1;
            s.currentChannelId = -1;
            RebuildServerRail();
            RebuildChannelList();
            RebuildMessageList();
        });
        panel.Children().Append(leaveBtn);

        dlg.Content(scroll);

        auto result = co_await dlg.ShowAsync();
        if (result == ContentDialogResult::Primary)
        {
            auto newName = winrt::to_string(nameBox.Text());
            if (!newName.empty() && newName != srv.name)
            {
                ::epyks_winui::GetClient().RenameServer(sid, newName);
                // Also update local state immediately so sidebar reflects the change
                ::epyks_winui::GetAppState().servers[sid].name = newName;
                RebuildServerRail();
                RebuildChannelList();
            }
        }
    }

    winrt::fire_and_forget ChatPage::MyProfileBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();

        ContentDialog dlg;
        dlg.XamlRoot(this->XamlRoot());
        dlg.Title(box_value(L"My Profile"));
        dlg.PrimaryButtonText(L"Save");
        dlg.CloseButtonText(L"Close");
        dlg.DefaultButton(ContentDialogButton::Primary);

        auto panel = StackPanel();
        panel.Spacing(12.0);
        panel.MinWidth(320);

        auto userLabel = TextBlock();
        userLabel.Text(winrt::to_hstring(state.username));
        userLabel.FontSize(18); userLabel.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        userLabel.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,242,243,245)));
        panel.Children().Append(userLabel);

        auto bioLabel = TextBlock();
        bioLabel.Text(L"BIO");
        bioLabel.FontSize(11); bioLabel.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        bioLabel.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,148,155,164)));
        panel.Children().Append(bioLabel);

        auto bioBox = TextBox();
        bioBox.Text(winrt::to_hstring(state.myProfile.bio));
        bioBox.PlaceholderText(L"Tell us about yourself...");
        bioBox.AcceptsReturn(true);
        bioBox.MinHeight(80);
        panel.Children().Append(bioBox);

        dlg.Content(panel);

        auto result = co_await dlg.ShowAsync();
        if (result == ContentDialogResult::Primary)
        {
            auto bio = winrt::to_string(bioBox.Text());
            ::epyks_winui::GetClient().UpdateProfile(bio, "");
        }
    }

    void ChatPage::SettingsBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        winrt::Windows::UI::Xaml::Interop::TypeName typeName{ winrt::hstring(winrt::name_of<winrt::epyks_winui::SettingsPage>()), winrt::Windows::UI::Xaml::Interop::TypeKind::Custom };
        Frame().Navigate(typeName);
    }

    void ChatPage::MuteBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();
        state.isMuted = !state.isMuted;
        ::epyks_winui::GetAudioClient().SetMute(state.isMuted);
        RefreshMyInfo();
    }

    void ChatPage::DeafenBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();
        state.isDeafened = !state.isDeafened;
        ::epyks_winui::GetAudioClient().SetDeafened(state.isDeafened);
        RefreshMyInfo();
    }

    void ChatPage::VoiceDisconnectBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();
        ::epyks_winui::GetClient().LeaveVoice();
        ::epyks_winui::GetAudioClient().StopVoice();
        
        // Clear current channel if it was the voice channel so we return to chat
        if (state.currentChannelId == state.voiceChannelId) {
            state.currentChannelId = -1;
        }

        state.inVoice = false;
        state.voiceServerId = -1;
        state.voiceChannelId = -1;
        
        // Hide voice panel and rebuild channel list to clear selection
        VoiceScreenPanel().Visibility(Visibility::Collapsed);
        MessageList().Visibility(Visibility::Visible);
        MessageInputPanel().Visibility(Visibility::Visible);
        
        RefreshMyInfo();
        state.FireChanged();
        RebuildChannelList();
        RebuildMessageList();
    }

    void ChatPage::MemberListToggle_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_memberListVisible = !m_memberListVisible;
        MemberCol().Width(m_memberListVisible
            ? GridLengthHelper::FromPixels(220)
            : GridLengthHelper::FromPixels(0));
        if (m_memberListVisible) RebuildMemberList();
    }

    void ChatPage::MessageInput_KeyDown(IInspectable const&, KeyRoutedEventArgs const& e)
    {
        if (e.Key() == VirtualKey::Enter && !e.KeyStatus().IsMenuKeyDown)
        {
            e.Handled(true);
            SendMessage();
        }
    }

    void ChatPage::SendBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SendMessage();
    }

    winrt::fire_and_forget ChatPage::ShowProfileFor(std::string username)
    {
        // Request fresh profile if not cached
        ::epyks_winui::GetClient().RequestProfile(username, true);

        ContentDialog dlg;
        dlg.XamlRoot(this->XamlRoot());
        dlg.CloseButtonText(L"Close");

        auto panel = StackPanel();
        panel.Spacing(10.0);
        panel.MinWidth(280);

        // Avatar + name row
        auto headerRow = StackPanel();
        headerRow.Orientation(Orientation::Horizontal);
        headerRow.Spacing(12.0);

        auto avatar = Button();
        avatar.Width(52); avatar.Height(52);
        avatar.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{26,26,26,26});
        avatar.BorderThickness(Thickness{0});
        avatar.Background(::epyks_winui::GetAvatarBrush(username));
        
        std::string cachePath = ::epyks_winui::GetCachePath(username + ".png");
        if (!std::filesystem::exists(cachePath))
        {
            auto avatarText = TextBlock();
            avatarText.Text(winrt::to_hstring(username.empty() ? "?" : username.substr(0, 1)));
            avatarText.FontSize(22); avatarText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
            avatarText.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,255,255,255)));
            avatarText.HorizontalAlignment(HorizontalAlignment::Center);
            avatarText.VerticalAlignment(VerticalAlignment::Center);
            avatar.Content(avatarText);
        }
        headerRow.Children().Append(avatar);

        auto nameCol = StackPanel();
        nameCol.VerticalAlignment(VerticalAlignment::Center);
        auto nameTb = TextBlock();
        nameTb.Text(winrt::to_hstring(username));
        nameTb.FontSize(18); nameTb.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        nameTb.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,242,243,245)));
        nameCol.Children().Append(nameTb);

        // Check online status from friends list
        auto& state = ::epyks_winui::GetAppState();
        bool isOnline = false;
        for (auto& f : state.friends) { if (f.username == username) { isOnline = f.online; break; } }
        auto statusTb = TextBlock();
        statusTb.Text(isOnline ? L"Online" : L"Offline");
        statusTb.FontSize(12);
        statusTb.Foreground(SolidColorBrush(isOnline
            ? winrt::Microsoft::UI::ColorHelper::FromArgb(255, 35, 165, 90)
            : winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
        nameCol.Children().Append(statusTb);
        headerRow.Children().Append(nameCol);
        panel.Children().Append(headerRow);

        // Bio from cache
        auto it = state.profileCache.find(username);
        if (it != state.profileCache.end() && !it->second.bio.empty())
        {
            auto sep = Border();
            sep.Height(1); sep.Background(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,63,65,71)));
            panel.Children().Append(sep);

            auto bioLabel = TextBlock();
            bioLabel.Text(L"ABOUT ME");
            bioLabel.FontSize(11); bioLabel.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            bioLabel.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,148,155,164)));
            panel.Children().Append(bioLabel);

            auto bioTb = TextBlock();
            bioTb.Text(winrt::to_hstring(it->second.bio));
            bioTb.FontSize(14); bioTb.TextWrapping(TextWrapping::WrapWholeWords);
            bioTb.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255,220,221,222)));
            panel.Children().Append(bioTb);
        }

        // DM button (if not own profile)
        if (username != state.username)
        {
            bool isFriend = false;
            for (auto& f : state.friends) {
                if (f.username == username) { isFriend = true; break; }
            }

            if (isFriend) {
                auto dmBtn = Button();
                dmBtn.Content(box_value(L"Send Message"));
                dmBtn.Style(Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
                dmBtn.HorizontalAlignment(HorizontalAlignment::Stretch);
                dmBtn.Click([this, username, &dlg](auto&&, auto&&) {
                    SelectDM(username);
                    dlg.Hide();
                });
                panel.Children().Append(dmBtn);
            }
        }

        dlg.Title(box_value(winrt::to_hstring(username)));
        dlg.Content(panel);
        co_await dlg.ShowAsync();
    }

    winrt::fire_and_forget ChatPage::AttachBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto& state = ::epyks_winui::GetAppState();
        if (state.currentServerId == -1 && state.currentDM.empty()) co_return;

        // Get the HWND via the stored app window HWND in AppState
        HWND hWnd = ::epyks_winui::GetAppState().mainWindowHwnd;
        if (hWnd == 0) co_return; // Safety check

        winrt::Windows::Storage::Pickers::FileOpenPicker picker;
        // Initialize with the window handle (required for packaged apps)
        picker.as<IInitializeWithWindow>()->Initialize(hWnd);

        picker.ViewMode(winrt::Windows::Storage::Pickers::PickerViewMode::Thumbnail);
        picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::PicturesLibrary);
        picker.FileTypeFilter().Append(L".png");
        picker.FileTypeFilter().Append(L".jpg");
        picker.FileTypeFilter().Append(L".jpeg");
        picker.FileTypeFilter().Append(L".gif");
        picker.FileTypeFilter().Append(L".webp");
        picker.FileTypeFilter().Append(L".mp4");
        picker.FileTypeFilter().Append(L"*");

        auto file = co_await picker.PickSingleFileAsync();
        if (!file) co_return;

        auto buffer = co_await winrt::Windows::Storage::FileIO::ReadBufferAsync(file);
        winrt::Windows::Storage::Streams::DataReader dataReader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
        std::vector<uint8_t> bytes(buffer.Length());
        dataReader.ReadBytes(bytes);

        ::epyks_winui::GetClient().UploadMedia(bytes);
    }

    void ChatPage::CancelReplyBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_replyToId = 0;
        m_replyToUser.clear();
        ReplyPreviewBar().Visibility(Visibility::Collapsed);
    }
}
}
}

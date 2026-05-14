#include "pch.h"
#include "SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "../ViewModels/AppState.h"
#include "../Network/EpyksClient.h"
#include "ChatPage.xaml.h"
#include "LoginPage.xaml.h"
#include "../../deps/miniaudio/miniaudio.h"
#include <shobjidl.h>
#include <winrt/Windows.Storage.Pickers.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::System;

namespace winrt::epyks_winui::implementation
{
    SettingsPage::SettingsPage()
    {
        InitializeComponent();
    }

    void SettingsPage::SetActiveTab(int tab)
    {
        SolidColorBrush active(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 64, 66, 73));
        SolidColorBrush inactive(winrt::Microsoft::UI::ColorHelper::FromArgb(0, 0, 0, 0));
        SolidColorBrush textActive(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 243, 245));
        SolidColorBrush textMuted(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164));

        AccountTabBtn().Background(tab == 0 ? active : inactive);
        VoiceTabBtn().Background(tab == 1 ? active : inactive);
        AppearanceTabBtn().Background(tab == 2 ? active : inactive);
        AccountTabBtn().Foreground(tab == 0 ? textActive : textMuted);
        VoiceTabBtn().Foreground(tab == 1 ? textActive : textMuted);
        AppearanceTabBtn().Foreground(tab == 2 ? textActive : textMuted);
    }

    void SettingsPage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        ShowAccountTab();
    }

    void SettingsPage::ShowAccountTab()
    {
        SetActiveTab(0);
        auto& state = ::epyks_winui::GetAppState();
        SettingsContent().Children().Clear();

        // Header
        auto hdr = TextBlock();
        hdr.Text(L"My Account");
        hdr.FontSize(20); hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        hdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 243, 245)));
        SettingsContent().Children().Append(hdr);

        // Username display
        auto usernameRow = StackPanel();
        usernameRow.Orientation(Orientation::Horizontal);
        usernameRow.Spacing(12);
        auto ub_label = TextBlock();
        ub_label.Text(L"Username:");
        ub_label.FontSize(14);
        ub_label.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
        ub_label.VerticalAlignment(VerticalAlignment::Center);
        auto ub_val = TextBlock();
        ub_val.Text(winrt::to_hstring(state.username));
        ub_val.FontSize(14);
        ub_val.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        ub_val.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 220, 221, 222)));
        ub_val.VerticalAlignment(VerticalAlignment::Center);
        usernameRow.Children().Append(ub_label);
        usernameRow.Children().Append(ub_val);
        SettingsContent().Children().Append(usernameRow);

        // Bio edit
        auto bioHdr = TextBlock();
        bioHdr.Text(L"BIO");
        bioHdr.FontSize(11); bioHdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        bioHdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
        bioHdr.Margin(Thickness{0, 12, 0, 6});
        SettingsContent().Children().Append(bioHdr);

        auto bioBox = TextBox();
        bioBox.Text(winrt::to_hstring(state.myProfile.bio));
        bioBox.PlaceholderText(L"Tell us about yourself...");
        bioBox.Background(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 56, 58, 64)));
        bioBox.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 220, 221, 222)));
        bioBox.BorderBrush(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 63, 65, 71)));
        bioBox.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{4,4,4,4});
        bioBox.AcceptsReturn(true);
        bioBox.MinHeight(80);
        SettingsContent().Children().Append(bioBox);

        // Save button
        auto saveBtn = Button();
        saveBtn.Content(box_value(L"Save Profile"));
        saveBtn.Style(Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
        saveBtn.HorizontalAlignment(HorizontalAlignment::Left);
        saveBtn.Padding(Thickness{16, 8, 16, 8});
        saveBtn.Click([this, bioBox](auto&&, auto&&) {
            std::string bio = winrt::to_string(bioBox.Text());
            ::epyks_winui::GetClient().UpdateProfile(bio, "");
        });
        SettingsContent().Children().Append(saveBtn);

        // -- Avatar section --
        auto avatarHdr = TextBlock();
        avatarHdr.Text(L"PROFILE PICTURE");
        avatarHdr.FontSize(11);
        avatarHdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        avatarHdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
        avatarHdr.Margin(Thickness{0, 20, 0, 8});
        SettingsContent().Children().Append(avatarHdr);

        auto avatarRow = StackPanel();
        avatarRow.Orientation(Orientation::Horizontal);
        avatarRow.Spacing(16);
        avatarRow.VerticalAlignment(VerticalAlignment::Center);

        // Current avatar circle
        auto avatarEll = winrt::Microsoft::UI::Xaml::Shapes::Ellipse();
        avatarEll.Width(72); avatarEll.Height(72);
        avatarEll.Fill(::epyks_winui::GetAvatarBrush(state.username));
        avatarRow.Children().Append(avatarEll);

        // Change button (fire_and_forget lambda wrapper)
        auto changeBtn = Button();
        changeBtn.Content(box_value(L"Change Avatar"));
        changeBtn.Style(Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
        changeBtn.Click([this](auto&&, auto&&) {
            [this]() -> winrt::fire_and_forget {
                winrt::Windows::Storage::Pickers::FileOpenPicker picker;
                auto initWithWindow = picker.as<IInitializeWithWindow>();
                if (initWithWindow) {
                    HWND hwnd = ::epyks_winui::GetAppState().mainWindowHwnd;
                    initWithWindow->Initialize(hwnd);
                }
                picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::PicturesLibrary);
                picker.FileTypeFilter().Append(L".png");
                picker.FileTypeFilter().Append(L".jpg");
                picker.FileTypeFilter().Append(L".jpeg");

                auto file = co_await picker.PickSingleFileAsync();
                if (!file) co_return;

                auto buf = co_await winrt::Windows::Storage::FileIO::ReadBufferAsync(file);
                std::vector<uint8_t> bytes(buf.Length());
                auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buf);
                reader.ReadBytes(bytes);

                // Save to local cache
                std::string cachePath = ::epyks_winui::GetCachePath(
                    ::epyks_winui::GetAppState().username + ".png");
                { std::ofstream f(cachePath, std::ios::binary);
                  if (f) f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }

                // Upload to server
                ::epyks_winui::GetClient().UploadPfp(bytes);
            }();
        });
        avatarRow.Children().Append(changeBtn);
        SettingsContent().Children().Append(avatarRow);
    }

    void SettingsPage::ShowVoiceTab()
    {
        SetActiveTab(1);
        SettingsContent().Children().Clear();

        auto hdr = TextBlock();
        hdr.Text(L"Voice & Video");
        hdr.FontSize(20); hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        hdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 243, 245)));
        SettingsContent().Children().Append(hdr);

        auto makeSectionLabel = [](const wchar_t* txt) {
            auto tb = TextBlock();
            tb.Text(txt);
            tb.FontSize(11); tb.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            tb.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 148, 155, 164)));
            tb.Margin(Thickness{0, 16, 0, 6});
            return tb;
        };

        // --- Input device ---
        auto inputCombo = ComboBox();
        inputCombo.HorizontalAlignment(HorizontalAlignment::Stretch);
        inputCombo.PlaceholderText(L"Default Microphone");

        // --- Output device ---
        auto outputCombo = ComboBox();
        outputCombo.HorizontalAlignment(HorizontalAlignment::Stretch);
        outputCombo.PlaceholderText(L"Default Speakers");

        // Enumerate devices via miniaudio
        {
            ma_context ctx;
            if (ma_context_init(nullptr, 0, nullptr, &ctx) == MA_SUCCESS)
            {
                ma_device_info* pCapture = nullptr;  ma_uint32 captureCount = 0;
                ma_device_info* pPlayback = nullptr; ma_uint32 playbackCount = 0;

                if (ma_context_get_devices(&ctx, &pPlayback, &playbackCount, &pCapture, &captureCount) == MA_SUCCESS)
                {
                    inputCombo.Items().Append(box_value(L"Default"));
                    for (ma_uint32 i = 0; i < captureCount; i++)
                        inputCombo.Items().Append(box_value(winrt::to_hstring(std::string(pCapture[i].name))));

                    outputCombo.Items().Append(box_value(L"Default"));
                    for (ma_uint32 i = 0; i < playbackCount; i++)
                        outputCombo.Items().Append(box_value(winrt::to_hstring(std::string(pPlayback[i].name))));
                }
                ma_context_uninit(&ctx);
            }
        }

        auto& state = ::epyks_winui::GetAppState();
        int inIdx = 0, outIdx = 0;
        for (uint32_t i = 0; i < inputCombo.Items().Size(); ++i) {
            if (winrt::to_string(winrt::unbox_value<winrt::hstring>(inputCombo.Items().GetAt(i))) == state.audioInDevice) inIdx = i;
        }
        for (uint32_t i = 0; i < outputCombo.Items().Size(); ++i) {
            if (winrt::to_string(winrt::unbox_value<winrt::hstring>(outputCombo.Items().GetAt(i))) == state.audioOutDevice) outIdx = i;
        }

        inputCombo.SelectedIndex(inIdx);
        outputCombo.SelectedIndex(outIdx);
        
        SettingsContent().Children().Append(makeSectionLabel(L"INPUT DEVICE"));
        SettingsContent().Children().Append(inputCombo);
        SettingsContent().Children().Append(makeSectionLabel(L"OUTPUT DEVICE"));
        SettingsContent().Children().Append(outputCombo);

        // --- Noise cancellation ---
        SettingsContent().Children().Append(makeSectionLabel(L"NOISE CANCELLATION"));
        auto ncRow = StackPanel();
        ncRow.Orientation(Orientation::Horizontal);
        ncRow.Spacing(12.0);
        auto ncLabel = TextBlock();
        ncLabel.Text(L"Enable RNNoise noise suppression");
        ncLabel.FontSize(14);
        ncLabel.VerticalAlignment(VerticalAlignment::Center);
        ncLabel.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 220, 221, 222)));
        auto ncToggle = ToggleSwitch();
        ncToggle.IsOn(true);
        ncRow.Children().Append(ncToggle);
        ncRow.Children().Append(ncLabel);
        SettingsContent().Children().Append(ncRow);

        // --- Input volume ---
        SettingsContent().Children().Append(makeSectionLabel(L"INPUT VOLUME"));
        auto inputVol = Slider();
        inputVol.Minimum(0); inputVol.Maximum(200); inputVol.Value(100);
        inputVol.HorizontalAlignment(HorizontalAlignment::Stretch);
        SettingsContent().Children().Append(inputVol);

        // --- Save button ---
        auto saveBtn = Button();
        saveBtn.Content(box_value(L"Save Voice Settings"));
        saveBtn.Style(Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
        saveBtn.Margin(Thickness{0, 16, 0, 0});
        saveBtn.Click([inputCombo, outputCombo](auto&&, auto&&) {
            std::string inDev = inputCombo.SelectedIndex() > 0
                ? winrt::to_string(winrt::unbox_value<winrt::hstring>(inputCombo.SelectedItem()))
                : "";
            std::string outDev = outputCombo.SelectedIndex() > 0
                ? winrt::to_string(winrt::unbox_value<winrt::hstring>(outputCombo.SelectedItem()))
                : "";
            auto& s = ::epyks_winui::GetAppState();
            s.audioInDevice = inDev;
            s.audioOutDevice = outDev;
            if (s.rememberMe)
                ::epyks_winui::SaveConfig(s.username, s.sessionToken, inDev, outDev, s.serverAddress);
            else
                ::epyks_winui::SaveConfig("", "", inDev, outDev, s.serverAddress);
        });
        SettingsContent().Children().Append(saveBtn);
    }

    void SettingsPage::ShowAppearanceTab()
    {
        SetActiveTab(2);
        SettingsContent().Children().Clear();

        auto hdr = TextBlock();
        hdr.Text(L"Appearance");
        hdr.FontSize(20); hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        hdr.Foreground(SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, 242, 243, 245)));
        SettingsContent().Children().Append(hdr);
    }

    void SettingsPage::GoBack()
    {
        if (Frame().CanGoBack()) Frame().GoBack();
        else
        {
            winrt::Windows::UI::Xaml::Interop::TypeName typeName{ winrt::hstring(winrt::name_of<winrt::epyks_winui::ChatPage>()), winrt::Windows::UI::Xaml::Interop::TypeKind::Custom };
            Frame().Navigate(typeName);
        }
    }

    void SettingsPage::AccountTabBtn_Click(IInspectable const&, RoutedEventArgs const&) { ShowAccountTab(); }
    void SettingsPage::VoiceTabBtn_Click(IInspectable const&, RoutedEventArgs const&) { ShowVoiceTab(); }
    void SettingsPage::AppearanceTabBtn_Click(IInspectable const&, RoutedEventArgs const&) { ShowAppearanceTab(); }

    void SettingsPage::LogOutBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ::epyks_winui::ClearConfig();
        ::epyks_winui::GetClient().Disconnect();
        ::epyks_winui::GetAppState() = {};
        winrt::Windows::UI::Xaml::Interop::TypeName typeName{ winrt::hstring(winrt::name_of<winrt::epyks_winui::LoginPage>()), winrt::Windows::UI::Xaml::Interop::TypeKind::Custom };
        Frame().Navigate(typeName);
    }

    void SettingsPage::CloseBtn_Click(IInspectable const&, RoutedEventArgs const&) { GoBack(); }

    void SettingsPage::Page_KeyDown(IInspectable const&, KeyRoutedEventArgs const& e)
    {
        if (e.Key() == VirtualKey::Escape) GoBack();
    }
}

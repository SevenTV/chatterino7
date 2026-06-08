#import <AppKit/AppKit.h>

#include "util/MacOsTitlebarButtons.hpp"

#include "Application.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "widgets/splits/RoomModeHelpers.hpp"

#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QSvgRenderer>

@class TitlebarActionTarget;

namespace chatterino {

using SplitPtr = Split;

namespace {

NSTitlebarAccessoryViewController *gAccessory = nil;
TitlebarActionTarget *gTarget = nil;
NSButton *gModButton = nil;
NSButton *gUsersButton = nil;
NSButton *gDropdownButton = nil;
NSButton *gModeButton = nil;
NSTextField *gTitleLabel = nil;

static constexpr CGFloat kBtnWidth = 28;
static constexpr CGFloat kBtnHeight = 21;
// Match the standard macOS titlebar height so the accessory fills the bar and
// buttons can be properly centered to align with the traffic-light controls.
static constexpr CGFloat kContainerHeight = 28;
static constexpr CGFloat kSpacing = 4;

/// Returns YES when macOS is in Dark Mode (Mojave+).
static BOOL isDarkMode(void)
{
    if (@available(macOS 10.14, *))
    {
        NSAppearanceName best = [NSApp.effectiveAppearance
            bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua,
                                               NSAppearanceNameDarkAqua]];
        return [best isEqualToString:NSAppearanceNameDarkAqua];
    }
    return NO;
}

/// Render a Qt SVG resource path to an NSImage at the requested point size.
static NSImage *nsImageFromQtSvg(const char *resourcePath, CGFloat ptSize)
{
    int px = static_cast<int>(ptSize * 2.0);
    QSvgRenderer renderer{QString{resourcePath}};
    QImage img{px, px, QImage::Format_ARGB32_Premultiplied};
    img.fill(Qt::transparent);
    {
        QPainter painter{&img};
        renderer.render(&painter);
    }
    CGImageRef cg = img.toCGImage();
    if (!cg)
        return nil;
    NSImage *ns = [[NSImage alloc] initWithCGImage:cg
                                              size:NSMakeSize(ptSize, ptSize)];
    CGImageRelease(cg);
    return ns;
}

/// Returns the SVG resource path for the moderation button.
/// Chooses the enabled/disabled and dark/light variant automatically.
static const char *modButtonSvgPath(bool moderationMode, BOOL dark)
{
    if (moderationMode)
        return dark ? ":/buttons/moderationEnabled-darkMode.svg"
                    : ":/buttons/moderationEnabled-lightMode.svg";
    return dark ? ":/buttons/moderationDisabled-darkMode.svg"
                : ":/buttons/moderationDisabled-lightMode.svg";
}

Split *currentSplit(SplitNotebook *notebook)
{
    if (!notebook)
    {
        return nullptr;
    }
    auto *page = notebook->getSelectedPage();
    return page ? page->getSelectedSplit() : nullptr;
}

// Repack the title label (left) then buttons left-to-right with no dead space,
// and resize the container to exactly fit.
void reflowTitlebarButtons(void)
{
    if (!gAccessory)
    {
        return;
    }

    NSView *container = gAccessory.view;
    CGFloat containerWidth = 0;

    // Title label — fixed max width so it never pushes buttons out of the bar.
    static constexpr CGFloat kLabelMaxWidth = 200;
    CGFloat labelX = 0;
    CGFloat labelW = 0;
    if (gTitleLabel && !gTitleLabel.hidden)
    {
        NSDictionary *attrs = @{NSFontAttributeName : gTitleLabel.font};
        CGFloat textW = std::ceil(
            [gTitleLabel.stringValue sizeWithAttributes:attrs].width);
        // Clamp to max so long titles do not steal space from buttons.
        labelW = std::min(textW + 8, kLabelMaxWidth);
        gTitleLabel.frame = NSMakeRect(labelX, 2, labelW, kContainerHeight - 4);
        containerWidth = labelW + kSpacing;
    }

    // Match SplitHeader layout order: mode → mod → chatters → dropdown
    NSButton *ordered[] = {gModeButton, gModButton, gUsersButton,
                           gDropdownButton};

    // Center buttons vertically so they align with the traffic-light controls.
    CGFloat btnY = std::round((kContainerHeight - kBtnHeight) / 2.0);

    CGFloat x = containerWidth;
    for (NSButton *btn : ordered)
    {
        if (!btn || btn.hidden)
        {
            continue;
        }

        CGFloat w = kBtnWidth;
        // Auto-size the mode label to fit its text so nothing gets clipped.
        if (btn == gModeButton && btn.title.length > 0)
        {
            NSDictionary *attrs = @{NSFontAttributeName : btn.font};
            CGFloat textW = std::ceil([btn.title sizeWithAttributes:attrs].width);
            w = textW + 10;  // 5 px padding each side
        }

        btn.frame = NSMakeRect(x, btnY, w, kBtnHeight);
        x += w + kSpacing;
    }

    // Strip trailing spacing if at least one button is shown.
    if (x > kSpacing)
    {
        x -= kSpacing;
    }

    containerWidth = std::max(x, containerWidth);

    // Resize the container so macOS knows the new accessory width.
    NSRect f = container.frame;
    f.size.width = std::max(containerWidth, kBtnWidth);
    container.frame = f;

    // Ask the window to re-lay-out its titlebar so the change is visible
    // immediately rather than on the next resize event.
    [container.window layoutIfNeeded];
}

}  // namespace

}  // namespace chatterino

@interface TitlebarActionTarget : NSObject
@property(nonatomic, assign) chatterino::SplitNotebook *notebook;
@property(nonatomic, assign) NSWindow *nsWindow;
@end

@implementation TitlebarActionTarget

- (void)toggleModeration:(id)sender
{
    auto *s = chatterino::currentSplit(self.notebook);
    if (!s)
    {
        return;
    }
    if (chatterino::getSettings()->moderationActions.empty())
    {
        chatterino::getApp()->getWindows()->showSettingsDialog(
            nullptr, chatterino::SettingsDialogPreference::ModerationActions);
        s->setModerationMode(true);
    }
    else
    {
        s->setModerationMode(!s->getModerationMode());
    }
}

- (void)openChatters:(id)sender
{
    auto *s = chatterino::currentSplit(self.notebook);
    if (s)
    {
        s->openChatterList();
    }
}

- (void)showModeMenu:(NSButton *)sender
{
    auto *split = chatterino::currentSplit(self.notebook);
    if (!split)
    {
        return;
    }

    // Convert button position to Qt screen coordinates (same as showDropdown:).
    NSRect btnFrame = [sender convertRect:sender.bounds toView:nil];
    NSRect screenRect = [self.nsWindow convertRectToScreen:btnFrame];
    NSScreen *screen = self.nsWindow.screen ?: [NSScreen mainScreen];
    CGFloat screenHeight = screen.frame.size.height;
    QPoint globalPos(static_cast<int>(NSMinX(screenRect)),
                     static_cast<int>(screenHeight - NSMinY(screenRect) + 2));

    split->showHeaderModeMenu(globalPos);
}

- (void)showDropdown:(NSButton *)sender
{
    auto *split = chatterino::currentSplit(self.notebook);
    if (split)
    {
        split->showHeaderDropdown();
    }
}

@end

namespace chatterino {

void setupMacOsTitlebarButtons(QWidget *window, SplitNotebook *notebook)
{
    if (gAccessory)
    {
        return;
    }

    NSView *nsView =
        (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (!nsView)
    {
        return;
    }
    NSWindow *nsWindow = nsView.window;
    if (!nsWindow)
    {
        return;
    }

    // Keep the window title visible so channel info (from updateCompactHeader)
    // can be shown in the native titlebar when compact headers is enabled.

    gTarget = [[TitlebarActionTarget alloc] init];
    gTarget.notebook = notebook;
    gTarget.nsWindow = nsWindow;

    NSView *container =
        [[NSView alloc] initWithFrame:NSMakeRect(0, 0, kBtnWidth, kContainerHeight)];

    auto makeBtn = [&](NSString *title, SEL action, NSString *tooltip) -> NSButton * {
        // Frames are set by reflowTitlebarButtons(); use a placeholder rect.
        NSButton *btn =
            [[NSButton alloc] initWithFrame:NSMakeRect(0, 1, kBtnWidth, kBtnHeight)];
        btn.title = title;
        btn.bordered = NO;
        btn.font = [NSFont systemFontOfSize:10];
        btn.target = gTarget;
        btn.action = action;
        btn.toolTip = tooltip;
        [container addSubview:btn];
        return btn;
    };

    // SVG icon size: kBtnHeight minus padding, matching SvgButton's default.
    CGFloat iconSize = kBtnHeight - 4;
    BOOL dark = isDarkMode();

    // Moderation toggle — SVG icon matches SplitHeader's moderationButton_.
    gModButton = makeBtn(@"", @selector(toggleModeration:),
                         @"Toggle moderation mode");
    gModButton.image = nsImageFromQtSvg(
        modButtonSvgPath(/*moderationMode=*/false, dark), iconSize);
    gModButton.imagePosition = NSImageOnly;
    gModButton.imageScaling = NSImageScaleProportionallyUpOrDown;

    // Chatter list — SVG icon matches SplitHeader's chattersButton_.
    gUsersButton = makeBtn(@"", @selector(openChatters:), @"Open chatter list");
    gUsersButton.image = nsImageFromQtSvg(
        dark ? ":/buttons/chatters-darkMode.svg"
             : ":/buttons/chatters-lightMode.svg",
        iconSize);
    gUsersButton.imagePosition = NSImageOnly;
    gUsersButton.imageScaling = NSImageScaleProportionallyUpOrDown;

    // Split actions dropdown — ⋮ (vertical ellipsis) matches DrawnButton::Kebab.
    gDropdownButton = makeBtn(@"\xE2\x8B\xAE", @selector(showDropdown:),
                              @"Split actions");
    // Room-modes label — clicking opens the same toggle menu as SplitHeader.
    gModeButton = makeBtn(@"", @selector(showModeMenu:), @"Room modes");

    gModButton.hidden = YES;
    gUsersButton.hidden = YES;
    gModeButton.hidden = YES;

    // Channel info label — sits to the left of the buttons in the accessory.
    gTitleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 2, 0, kContainerHeight - 4)];
    gTitleLabel.editable = NO;
    gTitleLabel.bordered = NO;
    gTitleLabel.backgroundColor = [NSColor clearColor];
    gTitleLabel.alignment = NSTextAlignmentRight;
    gTitleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    gTitleLabel.font = [NSFont systemFontOfSize:10];
    gTitleLabel.textColor = [NSColor labelColor];
    // Show the label only when compact headers is enabled.
    gTitleLabel.hidden = !getSettings()->compactHeaders.getValue();
    [container addSubview:gTitleLabel];

    gAccessory = [[NSTitlebarAccessoryViewController alloc] init];
    gAccessory.view = container;
    gAccessory.layoutAttribute = NSLayoutAttributeRight;

    // Compact the container to only the dropdown button before adding it.
    reflowTitlebarButtons();

    [nsWindow addTitlebarAccessoryViewController:gAccessory];

    // Dynamically hide the accessory view on very narrow windows to avoid
    // overlapping with traffic-light controls.
    {
        BOOL initiallyHidden = !getSettings()->compactHeaders.getValue();
        gAccessory.hidden = initiallyHidden;
        gAccessory.view.hidden = initiallyHidden;
    }
    // Hide the native titlebar text when compact headers is on — the
    // accessory view (label + buttons) replaces it.
    nsWindow.titleVisibility = getSettings()->compactHeaders.getValue()
                                   ? NSWindowTitleHidden
                                   : NSWindowTitleVisible;

    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidResizeNotification
        object:nsWindow
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(NSNotification *note) {
            if (!gAccessory)
                return;
            BOOL hidden = !getSettings()->compactHeaders.getValue();
            gAccessory.hidden = hidden;
            gAccessory.view.hidden = hidden;
        }];
}

void setMacOsTitlebarButtonsVisible(bool visible)
{
    if (!gAccessory)
        return;

    if (!visible)
    {
        if (gModeButton) gModeButton.hidden = YES;
        if (gModButton) gModButton.hidden = YES;
        if (gUsersButton) gUsersButton.hidden = YES;
        if (gDropdownButton) gDropdownButton.hidden = YES;
        if (gTitleLabel) gTitleLabel.hidden = YES;
        reflowTitlebarButtons();
    }
    else
    {
        if (gDropdownButton) gDropdownButton.hidden = NO;
        if (gTitleLabel) gTitleLabel.hidden = NO;
        reflowTitlebarButtons();
    }

    BOOL hidden = !visible;
    gAccessory.hidden = hidden;
    gAccessory.view.hidden = hidden;
}

void setMacOsTitlebarLabelText(const QString &text)
{
    if (!gTitleLabel)
    {
        return;
    }
    gTitleLabel.stringValue = text.toNSString();
    reflowTitlebarButtons();
}

void setMacOsTitlebarTitleVisible(bool visible)
{
    NSWindow *nsWindow = gAccessory ? gAccessory.view.window : nil;
    if (!nsWindow)
    {
        return;
    }
    nsWindow.titleVisibility = visible ? NSWindowTitleVisible
                                       : NSWindowTitleHidden;
}

void updateMacOsTitlebarButtonsForSplit(Split *split)
{
    if (!gModButton || !gUsersButton || !gModeButton || !gDropdownButton)
    {
        return;
    }

    auto channel = split ? split->getSelectedChannel() : nullptr;
    if (!channel || !channel->isTwitchOrKickChannel())
    {
        gModButton.hidden = YES;
        gUsersButton.hidden = YES;
        gModeButton.hidden = YES;
        reflowTitlebarButtons();
        return;
    }

    bool hasMod = channel->hasModRights();
    bool moderationMode =
        split->getModerationMode() &&
        !getSettings()->moderationActions.empty();

    gModButton.hidden = !(hasMod || moderationMode);
    gUsersButton.hidden = !(hasMod && channel->isTwitchChannel());

    BOOL dark = isDarkMode();
    gModButton.image = nsImageFromQtSvg(
        modButtonSvgPath(moderationMode, dark),
        kBtnHeight - 4);

    gUsersButton.image = nsImageFromQtSvg(
        dark ? ":/buttons/chatters-darkMode.svg"
             : ":/buttons/chatters-lightMode.svg",
        kBtnHeight - 4);

    QString modeText;
    if (auto *twitchChannel = dynamic_cast<TwitchChannel *>(channel.get()))
    {
        auto roomModes = twitchChannel->accessRoomModes();
        modeText = formatRoomModeUnclean(*roomModes);
        cleanRoomModeText(modeText, twitchChannel->hasModRights());
    }
    else if (auto *kickChannel = dynamic_cast<KickChannel *>(channel.get()))
    {
        modeText = formatRoomModeUnclean(kickChannel->roomModes());
        cleanRoomModeText(modeText, false);
    }

    if (!modeText.isEmpty())
    {
        gModeButton.title = modeText.toNSString();
        gModeButton.hidden = NO;
    }
    else
    {
        gModeButton.hidden = YES;
    }

    reflowTitlebarButtons();
}

}  // namespace chatterino

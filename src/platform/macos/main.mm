// Composition root: the application, its menus and the set of open documents.
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#import "DocumentWindow.h"

@interface CEAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation CEAppDelegate {
    NSMutableArray<CEDocumentWindow*>* _documents;
    NSMutableArray<NSString*>* _closedPaths;  // most recent last, for Reopen Closed Tab
}

- (instancetype)init {
    if ((self = [super init])) {
        _documents = [NSMutableArray array];
        _closedPaths = [NSMutableArray array];
    }
    return self;
}

- (void)adopt:(CEDocumentWindow*)document {
    if (!document) return;
    __weak CEAppDelegate* weakSelf = self;
    document.onClose = ^(CEDocumentWindow* closed) {
        // Out of the list at once, so quitting never asks about it, but released only once its window
        // has finished closing: the block below holds it until then.
        dispatch_async(dispatch_get_main_queue(), ^{ (void)closed; });
        CEAppDelegate* app = weakSelf;
        if (!app) return;
        [app->_documents removeObject:closed];
        if (closed.path) [app->_closedPaths addObject:closed.path];
    };
    [_documents addObject:document];
    [document.window makeKeyAndOrderFront:nil];
}

- (void)openPath:(NSString*)path {
    path = path.stringByStandardizingPath;
    for (CEDocumentWindow* document in _documents) {
        if ([document.path isEqualToString:path]) return [document.window makeKeyAndOrderFront:nil];
    }
    CEDocumentWindow* opened = [CEDocumentWindow openPath:path];
    if (!opened) return;
    CEDocumentWindow* pristine = nil;
    for (CEDocumentWindow* document in _documents)
        if (document.isPristine) pristine = document;
    if (pristine) [opened.window setFrame:pristine.window.frame display:NO];
    [self adopt:opened];
    [pristine.window close];
}

#pragma mark - NSApplicationDelegate

- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    (void)application;
    for (NSURL* url in urls)
        if (url.isFileURL) [self openPath:url.path];
}

- (BOOL)applicationShouldOpenUntitledFile:(NSApplication*)sender { (void)sender; return YES; }

- (BOOL)applicationOpenUntitledFile:(NSApplication*)sender {
    (void)sender;
    [self newDocument:nil];
    return YES;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)visible {
    (void)sender;
    if (!visible && _documents.count == 0) [self newDocument:nil];
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { (void)sender; return YES; }

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    for (CEDocumentWindow* document in [_documents copy])
        if (![document confirmClose]) return NSTerminateCancel;
    return NSTerminateNow;
}

#pragma mark - File menu

- (void)newDocument:(id)sender {
    (void)sender;
    [self adopt:[CEDocumentWindow untitled]];
}

- (void)newTab:(id)sender {
    (void)sender;
    [self newDocument:nil];
}

- (void)openDocument:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[ UTTypeCSource, UTTypeCHeader, UTTypeCPlusPlusSource, UTTypeCPlusPlusHeader ];
    panel.allowsMultipleSelection = YES;
    if ([panel runModal] != NSModalResponseOK) return;
    for (NSURL* url in panel.URLs) [self openPath:url.path];
}

- (void)reopenClosedTab:(id)sender {
    (void)sender;
    while (NSString* path = _closedPaths.lastObject) {
        [_closedPaths removeLastObject];
        if ([NSFileManager.defaultManager fileExistsAtPath:path]) return [self openPath:path];
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(reopenClosedTab:)) return _closedPaths.count > 0;
    return YES;
}

#pragma mark - Window menu

// ⌘1…⌘8 show that tab of the front document window and ⌘9 its last one, as in Safari.
- (void)selectTab:(NSMenuItem*)sender {
    for (NSWindow* window in NSApp.orderedWindows) {
        if (![window.tabbingIdentifier isEqualToString:CEDocumentTabs]) continue;
        NSArray<NSWindow*>* tabs = window.tabbedWindows ? window.tabbedWindows : @[ window ];
        const NSUInteger index = sender.tag == 9 ? tabs.count - 1 : static_cast<NSUInteger>(sender.tag - 1);
        if (index < tabs.count) [tabs[index] makeKeyAndOrderFront:nil];
        return;
    }
}

@end

namespace {

NSMenuItem* item(NSMenu* menu, NSString* title, SEL action, NSString* key,
                 NSEventModifierFlags modifiers = NSEventModifierFlagCommand) {
    NSMenuItem* entry = [menu addItemWithTitle:title action:action keyEquivalent:key];
    entry.keyEquivalentModifierMask = modifiers;
    return entry;
}

NSMenu* submenu(NSMenu* bar, NSString* title) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
    [bar addItemWithTitle:title action:nil keyEquivalent:@""].submenu = menu;
    return menu;
}

NSMenu* build_menus() {
    NSMenu* bar = [[NSMenu alloc] init];

    NSMenu* app = submenu(bar, @"C Editor");
    item(app, @"C Editor Hakkında", @selector(orderFrontStandardAboutPanel:), @"");
    [app addItem:NSMenuItem.separatorItem];
    item(app, @"C Editor’ü Gizle", @selector(hide:), @"h");
    item(app, @"Diğerlerini Gizle", @selector(hideOtherApplications:), @"h", NSEventModifierFlagCommand | NSEventModifierFlagOption);
    item(app, @"Tümünü Göster", @selector(unhideAllApplications:), @"");
    [app addItem:NSMenuItem.separatorItem];
    item(app, @"C Editor’den Çık", @selector(terminate:), @"q");

    NSMenu* file = submenu(bar, @"Dosya");
    item(file, @"Yeni", @selector(newDocument:), @"n");
    item(file, @"Yeni Sekme", @selector(newTab:), @"t");
    item(file, @"Aç…", @selector(openDocument:), @"o");
    item(file, @"Kapatılan Sekmeyi Yeniden Aç", @selector(reopenClosedTab:), @"t", NSEventModifierFlagCommand | NSEventModifierFlagShift);
    [file addItem:NSMenuItem.separatorItem];
    item(file, @"Kapat", @selector(performClose:), @"w");
    item(file, @"Kaydet", @selector(saveDocument:), @"s");
    item(file, @"Farklı Kaydet…", @selector(saveDocumentAs:), @"s", NSEventModifierFlagCommand | NSEventModifierFlagOption);

    NSMenu* edit = submenu(bar, @"Düzen");
    item(edit, @"Geri Al", @selector(undo:), @"z");
    item(edit, @"Yinele", @selector(redo:), @"z", NSEventModifierFlagCommand | NSEventModifierFlagShift);
    [edit addItem:NSMenuItem.separatorItem];
    item(edit, @"Kes", @selector(cut:), @"x");
    item(edit, @"Kopyala", @selector(copy:), @"c");
    item(edit, @"Yapıştır", @selector(paste:), @"v");
    item(edit, @"Tümünü Seç", @selector(selectAll:), @"a");

    NSMenu* format = submenu(bar, @"Biçim");
    NSMenu* encoding = [[NSMenu alloc] initWithTitle:@"Kodlama"];
    item(encoding, @"UTF-8", @selector(useUTF8:), @"");
    item(encoding, @"Windows-1254 (Türkçe)", @selector(useWindows1254:), @"");
    [format addItemWithTitle:@"Kodlama" action:nil keyEquivalent:@""].submenu = encoding;
    NSMenu* lineEnds = [[NSMenu alloc] initWithTitle:@"Satır Sonları"];
    item(lineEnds, @"LF (macOS)", @selector(useLF:), @"");
    item(lineEnds, @"CRLF (Windows)", @selector(useCRLF:), @"");
    [format addItemWithTitle:@"Satır Sonları" action:nil keyEquivalent:@""].submenu = lineEnds;

    NSMenu* run = submenu(bar, @"Çalıştır");
    item(run, @"Derle ve Çalıştır", @selector(run:), @"r");
    NSMenuItem* f11 = item(run, @"Derle ve Çalıştır", @selector(run:), [NSString stringWithFormat:@"%C", (unichar)NSF11FunctionKey], 0);
    f11.hidden = YES;
    f11.allowsKeyEquivalentWhenHidden = YES;

    NSMenu* window = submenu(bar, @"Pencere");
    item(window, @"Simge Durumuna Küçült", @selector(performMiniaturize:), @"m");
    item(window, @"Büyüt", @selector(performZoom:), @"");
    [window addItem:NSMenuItem.separatorItem];
    item(window, @"Sonraki Sekmeyi Göster", @selector(selectNextTab:), @"\t", NSEventModifierFlagControl);
    item(window, @"Önceki Sekmeyi Göster", @selector(selectPreviousTab:), @"\t", NSEventModifierFlagControl | NSEventModifierFlagShift);
    for (NSInteger n = 1; n <= 9; ++n) {
        NSMenuItem* tab = item(window, [NSString stringWithFormat:@"%ld. Sekmeyi Göster", (long)n], @selector(selectTab:),
                               [NSString stringWithFormat:@"%ld", (long)n]);
        tab.tag = n;
        tab.hidden = YES;
        tab.allowsKeyEquivalentWhenHidden = YES;
    }
    NSApp.windowsMenu = window;
    return bar;
}

}  // namespace

int main() {
    @autoreleasepool {
        NSApplication* application = NSApplication.sharedApplication;
        application.activationPolicy = NSApplicationActivationPolicyRegular;
        CEAppDelegate* delegate = [[CEAppDelegate alloc] init];
        application.delegate = delegate;
        application.mainMenu = build_menus();
        [application run];
    }
    return 0;
}

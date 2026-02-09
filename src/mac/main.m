#import <Cocoa/Cocoa.h>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static NSStatusItem *gStatusItem;
static NSMenu       *gMenu;
static NSObject     *gOutputLock;
static NSString     *gInitialIcon;
static NSString     *gInitialTooltip;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void emit(NSString *method, NSDictionary *params) {
    @synchronized (gOutputLock) {
        NSMutableDictionary *msg = [NSMutableDictionary dictionary];
        msg[@"method"] = method;
        if (params) msg[@"params"] = params;
        
        NSData *data = [NSJSONSerialization dataWithJSONObject:msg options:0 error:nil];
        if (data) {
            fwrite(data.bytes, 1, data.length, stdout);
            fputc('\n', stdout);
            fflush(stdout);
        }
    }
}

// Vector-based icon drawing: sharper on Retina and smaller binary footprint
static NSImage *createDefaultIcon() {
    return [NSImage imageWithSize:NSMakeSize(22, 22) flipped:NO drawingHandler:^BOOL(NSRect dstRect) {
        [[NSColor colorWithRed:0.18 green:0.68 blue:0.20 alpha:1.0] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(dstRect, 3, 3)] fill];
        return YES;
    }];
}

static NSImage *processImage(NSData *data) {
    NSImage *img = [[NSImage alloc] initWithData:data];
    if (img) {
        [img setSize:NSMakeSize(22, 22)];
        [img setTemplate:YES]; // Allows Dark/Light mode switching
    }
    return img;
}

// ---------------------------------------------------------------------------
// Menu Implementation
// ---------------------------------------------------------------------------

@interface TrayMenuTarget : NSObject <NSMenuDelegate> @end
@implementation TrayMenuTarget
- (void)menuItemClicked:(NSMenuItem *)sender {
    if (sender.representedObject) emit(@"clicked", @{@"id": sender.representedObject});
}
- (void)menuWillOpen:(NSMenu *)menu { emit(@"menuRequested", nil); }
@end

static TrayMenuTarget *gTarget;

static void buildMenuItems(NSMenu *menu, NSArray *items) {
    for (NSDictionary *cfg in items) {
        @autoreleasepool {
            if ([cfg[@"separator"] boolValue]) {
                [menu addItem:[NSMenuItem separatorItem]];
                continue;
            }

            NSMenuItem *mi = [[NSMenuItem alloc] initWithTitle:cfg[@"title"] ?: @""
                                                        action:@selector(menuItemClicked:)
                                                 keyEquivalent:@""];
            mi.target = gTarget;
            mi.representedObject = cfg[@"id"];
            mi.toolTip = cfg[@"tooltip"];
            mi.enabled = cfg[@"enabled"] ? [cfg[@"enabled"] boolValue] : YES;
            if ([cfg[@"checked"] boolValue]) mi.state = NSControlStateValueOn;

            NSArray *children = cfg[@"items"];
            if (children.count > 0) {
                NSMenu *sub = [[NSMenu alloc] initWithTitle:cfg[@"title"] ?: @""];
                buildMenuItems(sub, children);
                mi.submenu = sub;
            }
            [menu addItem:mi];
        }
    }
}

// ---------------------------------------------------------------------------
// IO Logic
// ---------------------------------------------------------------------------

static void stdinReaderThread(void) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    
    while ((len = getline(&line, &cap, stdin)) > 0) {
        @autoreleasepool {
            if (line[len - 1] == '\n') line[--len] = '\0';
            if (len == 0) continue;

            NSData *data = [NSData dataWithBytesNoCopy:line length:len freeWhenDone:NO];
            NSDictionary *msg = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            if (!msg) continue;

            NSString *method = msg[@"method"];
            NSDictionary *params = msg[@"params"];

            if ([method isEqualToString:@"setIcon"]) {
                NSData *iconData = [[NSData alloc] initWithBase64EncodedString:params[@"base64"] ?: @"" options:0];
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (iconData) gStatusItem.button.image = processImage(iconData);
                });
            } else {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if ([method isEqualToString:@"setMenu"]) {
                        [gMenu removeAllItems];
                        buildMenuItems(gMenu, params[@"items"]);
                    } else if ([method isEqualToString:@"setTooltip"]) {
                        gStatusItem.button.toolTip = params[@"text"];
                    } else if ([method isEqualToString:@"quit"]) {
                        [NSApp terminate:nil];
                    }
                });
            }
        }
    }
    free(line);
    dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
}

// ---------------------------------------------------------------------------
// Entry Point
// ---------------------------------------------------------------------------

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        gOutputLock = [[NSObject alloc] init];
        gTarget = [[TrayMenuTarget alloc] init];
        
        // Simple arg parser
        for (int i = 1; i < argc; i++) {
            NSString *arg = [NSString stringWithUTF8String:argv[i]];
            if ([arg isEqualToString:@"--icon"] && i + 1 < argc) gInitialIcon = [NSString stringWithUTF8String:argv[++i]];
            if ([arg isEqualToString:@"--tooltip"] && i + 1 < argc) gInitialTooltip = [NSString stringWithUTF8String:argv[++i]];
        }

        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];

        gStatusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
        gMenu = [[NSMenu alloc] init];
        gMenu.delegate = gTarget;
        gStatusItem.menu = gMenu;
        
        @autoreleasepool {
            NSImage *img = gInitialIcon ? [[NSImage alloc] initWithContentsOfFile:gInitialIcon] : nil;
            gStatusItem.button.image = img ? processImage([img TIFFRepresentation]) : createDefaultIcon();
            gStatusItem.button.toolTip = gInitialTooltip ?: @"Tray";
        }

        emit(@"ready", nil);
        [NSThread detachNewThreadWithBlock:^{ stdinReaderThread(); }];
        [app run];
    }
    return 0;
}

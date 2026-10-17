#include "TouchSurface.h"
#import <AppKit/AppKit.h>

@interface PrismaTouchCaptureView : NSView
{
@public
    TouchSurface* owner;
    BOOL capturing;
    NSMutableDictionary* tokens;
    uint64_t nextToken;
}
- (void) cancelContacts;
- (void) windowResigned:(NSNotification*)notification;
- (void) deliver:(NSEvent*)event phase:(NSTouchPhase)phase kind:(int)kind;
@end

@implementation PrismaTouchCaptureView
- (instancetype) initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        tokens = [[NSMutableDictionary alloc] init]; nextToken = 0;
        self.wantsRestingTouches = YES;
        self.allowedTouchTypes = 0;
    }
    return self;
}
- (BOOL) isOpaque { return NO; }
- (BOOL) acceptsFirstResponder { return NO; }
- (NSView*) hitTest:(NSPoint)point { return capturing ? [super hitTest:point] : nil; }
// AppKit tap-to-click may generate a mouse event for a touch already handled below.
- (void) mouseDown:(NSEvent*)event { juce::ignoreUnused (event); }
- (void) mouseUp:(NSEvent*)event { juce::ignoreUnused (event); }
- (void) scrollWheel:(NSEvent*)event { juce::ignoreUnused (event); }
- (void) viewWillMoveToWindow:(NSWindow*)window
{
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowDidResignKeyNotification object:nil];
    [self cancelContacts];
    if (window != nil)
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowResigned:)
            name:NSWindowDidResignKeyNotification object:window];
    [super viewWillMoveToWindow:window];
}
- (void) windowResigned:(NSNotification*)notification
{
    juce::ignoreUnused (notification);
    [self cancelContacts];
    if (capturing && owner && owner->onCancel) owner->onCancel();
}
- (void) cancelContacts
{
    if (owner && owner->onTouch)
        for (NSNumber* token in [tokens allValues]) owner->onTouch ([token unsignedLongLongValue],0,0,2);
    [tokens removeAllObjects];
}
- (void) deliver:(NSEvent*)event phase:(NSTouchPhase)phase kind:(int)kind
{
    if (! capturing || ! self.window.isKeyWindow || ! owner) return;
    for (NSTouch* touch in [event touchesMatchingPhase:phase inView:self])
    {
        if (touch.type != NSTouchTypeIndirect) continue;
        NSNumber* token = [tokens objectForKey:touch.identity];
        if (kind == 0)
        {
            if (token != nil) continue;
            token = [NSNumber numberWithUnsignedLongLong:++nextToken];
            [tokens setObject:token forKey:touch.identity];
        }
        if (token == nil) continue;
        NSPoint p = touch.normalizedPosition;
        if (owner->onTouch) owner->onTouch ([token unsignedLongLongValue], (float) p.x, (float) p.y, kind);
        if (kind == 2) [tokens removeObjectForKey:touch.identity];
    }
}
- (void) touchesBeganWithEvent:(NSEvent*)event { [self deliver:event phase:NSTouchPhaseBegan kind:0]; }
- (void) touchesMovedWithEvent:(NSEvent*)event { [self deliver:event phase:NSTouchPhaseMoved kind:1]; }
- (void) touchesEndedWithEvent:(NSEvent*)event { [self deliver:event phase:NSTouchPhaseEnded kind:2]; }
- (void) touchesCancelledWithEvent:(NSEvent*)event { [self deliver:event phase:NSTouchPhaseCancelled kind:2]; }
- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [tokens release];
    [super dealloc];
}
@end

TouchSurface::TouchSurface()
{
    setInterceptsMouseClicks (false,false);
    auto* view = [[PrismaTouchCaptureView alloc] initWithFrame:NSZeroRect];
    view->owner = this; native = view; setView (view); [view release];
}
TouchSurface::~TouchSurface()
{
    auto* view = static_cast<PrismaTouchCaptureView*> (native);
    view->owner = nullptr; view->capturing = NO; setView (nullptr); native = nullptr;
}
void TouchSurface::setCapturing (bool enabled)
{
    auto* view = static_cast<PrismaTouchCaptureView*> (native);
    view->capturing = enabled;
    view.allowedTouchTypes = enabled ? NSTouchTypeMaskIndirect : 0;
    if (! enabled) [view cancelContacts];
}

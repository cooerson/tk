/*
 * tkMacOSXSysTray.c --
 *
 *	tkMacOSXSysTray.c implements a "systray" Tcl command which allows
 *      one to change the system tray/taskbar icon of a Tk toplevel
 *      window and a "sysnotify" command to post system notifications.
 *      In macOS the icon appears on the right hand side of the menu bar.
 *
 * Copyright (c) 2020 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tkInt.h>
#include <tkMacOSXInt.h>
#include "tkMacOSXPrivate.h"

/*
 * Prior to macOS 10.14 user notifications were handled by the NSApplication's
 * NSUserNotificationCenter via a NSUserNotificationCenterDelegate object.
 * These classes were defined in the CoreFoundation framework.  In macOS 10.14
 * a separate UserNotifications framework was introduced which adds some
 * additional functionality, primarily allowing users to opt out of receiving
 * notifications on a per app basis.  This framework uses a different class,
 * the UNUserNotificationCenter and its delegate follows a different protocol,
 * named UNUserNotificationCenterDelegate.
 *
 * In macOS 11.0 the NSUserNotificationCenter and its delegate protocol were
 * deprecated.  So in this file we implemented both protocols, with the intent
 * of using the UserNotifications.framework on systems which provide it.
 * Unfortunately, however, this does not work.  The UNUserNotificationCenter
 * never authorizes Wish to send notifications, regardless of how the settings
 * in the user's Notification preferences are configured.  Consequently, until
 * this is fixed, we must disable using the framework and put up with many
 * deprecations when building Tk for a minimum target of 11.0.
 *
 * To disable using the UserNotifications.framework on systems that support it,
 * define DISABLE_NOTIFICATION_FRAMEWORK.  To test the broken new framework
 * remove the following directive.
 */

#define DISABLE_NOTIFICATION_FRAMEWORK

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400

#import <UserNotifications/UserNotifications.h>
static NSString *TkNotificationCategory;

#endif

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000

/*
 * Class declaration for TkStatusItem.
 */

@interface TkStatusItem: NSObject {
    NSStatusItem * statusItem;
    NSStatusBar * statusBar;
    NSImage * icon;
    NSString * tooltip;
    Tcl_Interp * interp;
    Tcl_Obj * b1_callback;
    Tcl_Obj * b3_callback;
}

- (id) init : (Tcl_Interp *) interp;
- (void) setImagewithImage : (NSImage *) image;
- (void) setTextwithString : (NSString *) string;
- (void) setB1Callback : (Tcl_Obj *) callback;
- (void) setB3Callback : (Tcl_Obj *) callback;
- (void) clickOnStatusItem: (id) sender;
- (void) dealloc;

@end

/*
 * Class declaration for TkUserNotifier. A TkUserNotifier object has no
 * attributes but implements the NSUserNotificationCenterDelegate protocol or
 * the UNNotificationCenterDelegate protocol, as appropriate for the runtime
 * environment.  It also has one additional method which posts a user
 * notification. There is one TkUserNotifier for the application, shared by all
 * interpreters.
 */

@interface TkUserNotifier: NSObject {
}

/*
 * This method is used to post a notification.
 */

- (void) postNotificationWithTitle : (NSString *) title message: (NSString *) detail;

/*
 * The following methods comprise the NSUserNotificationCenterDelegate protocol.
 */

#if MAC_OS_X_VERSION_MIN_REQUIRED < 110000

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
    didDeliverNotification:(NSUserNotification *)notification;

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
    didActivateNotification:(NSUserNotification *)notification;

- (BOOL) userNotificationCenter:(NSUserNotificationCenter *)center
    shouldPresentNotification:(NSUserNotification *)notification;

#endif

/*
 * The following methods comprise the UNNotificationCenterDelegate protocol:
 */

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101400

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
    withCompletionHandler:(void (^)(void))completionHandler;

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    willPresentNotification:(UNNotification *)notification
    withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler;

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
   openSettingsForNotification:(UNNotification *)notification;

#endif

@end

/*
 * The singleton instance of TkUserNotifier for the application is stored in
 * this static variable.
 */

static TkUserNotifier *notifier = nil;

/*
 * Class declaration for TkStatusItem. A TkStatusItem represents an icon posted
 * on the status bar located on the right side of the MenuBar.  Each interpreter
 * may have at most one TkStatusItem.  A pointer to the TkStatusItem belonging
 * to an interpreter is stored as the clientData of the MacSystrayObjCmd instance
 * in that interpreter.  It will be NULL until the tk systray command is executed
 * in the interpreter.
 */

@implementation TkStatusItem : NSObject

- (id) init : (Tcl_Interp *) interpreter {
    [super init];
    statusBar = [NSStatusBar systemStatusBar];
    statusItem = [[statusBar statusItemWithLength:NSVariableStatusItemLength] retain];
    statusItem.button.target = self;
    statusItem.button.action = @selector(clickOnStatusItem: );
    [statusItem.button sendActionOn : NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
    statusItem.visible = YES;
    interp = interpreter;
    b1_callback = NULL;
    b3_callback = NULL;
    return self;
}

- (void) setImagewithImage : (NSImage *) image
{
    icon = nil;
    icon = image;
    statusItem.button.image = icon;
}

- (void) setTextwithString : (NSString *) string
{
    tooltip = nil;
    tooltip = string;
    statusItem.button.toolTip = tooltip;
}

- (void) setB1Callback : (Tcl_Obj *) obj
{
    if (obj != NULL) {
	Tcl_IncrRefCount(obj);
    }
    if (b1_callback != NULL) {
	Tcl_DecrRefCount(b1_callback);
    }
    b1_callback = obj;
}

- (void) setB3Callback : (Tcl_Obj *) obj
{
    if (obj != NULL) {
	Tcl_IncrRefCount(obj);
    }
    if (b3_callback != NULL) {
	Tcl_DecrRefCount(b3_callback);
    }
    b3_callback = obj;
}

- (void) clickOnStatusItem: (id) sender
{
    NSEvent *event = [NSApp currentEvent];
    if (([event type] == NSEventTypeLeftMouseUp) && (b1_callback != NULL)) {
	int result = Tcl_EvalObjEx(interp, b1_callback, TCL_EVAL_GLOBAL);
	if (result != TCL_OK) {
	    Tcl_BackgroundException(interp, result);
	}
    } else {
	if (([event type] == NSEventTypeRightMouseUp) && (b3_callback != NULL)) {
	    int result = Tcl_EvalObjEx(interp, b3_callback, TCL_EVAL_GLOBAL);
	    if (result != TCL_OK) {
		Tcl_BackgroundException(interp, result);
	    }
	}
    }
}
- (void) dealloc
{
    [statusBar removeStatusItem: statusItem];
    if (b1_callback != NULL) {
	Tcl_DecrRefCount(b1_callback);
    }
    if (b3_callback != NULL) {
	Tcl_DecrRefCount(b3_callback);
    }
    [super dealloc];
}

@end

/*
 * Type used for the ClientData of a MacSystrayObjCmd instance.
 */

typedef TkStatusItem** StatusItemInfo;

@implementation TkUserNotifier : NSObject

-  (void) postNotificationWithTitle : (NSString * ) title
			     message: (NSString * ) detail
{

#if !defined(DISABLE_NOTIFICATION_FRAMEWORK)

    if (@available(macOS 10.14, *)) {
	UNUserNotificationCenter *center;
	UNMutableNotificationContent* content;
	UNNotificationRequest *request;
	__block Bool authorized = NO;

	center = [UNUserNotificationCenter currentNotificationCenter];
	[center requestAuthorizationWithOptions: UNAuthorizationOptionProvisional
		completionHandler: ^(BOOL granted, NSError* _Nullable error)
		{
		    authorized = granted;
		    if (error) {
			fprintf(stderr,
				"Authorization failed with error: %s\n",
				[NSString stringWithFormat:@"%@",
					  error.userInfo].UTF8String);
		    }
		}];
	if (authorized) {
	    content = [[UNMutableNotificationContent alloc] init];
	    content.title = title;
	    content.body = detail;
	    content.sound = [UNNotificationSound defaultSound];
	    content.categoryIdentifier = TkNotificationCategory;
	    request = [UNNotificationRequest
			  requestWithIdentifier:@"TkNotificationID"
					content:content
					trigger:nil
		       ];
	    center.delegate = (id) self;
	    fprintf(stderr, "Adding notification after authorization\n");
	    [center addNotificationRequest: request
		     withCompletionHandler: ^(NSError* _Nullable error) {
		    if (error) {
			fprintf(stderr,
				"addNotificationRequest: error = %s\n",
				[NSString stringWithFormat:@"%@",
					  error.userInfo].UTF8String);
		    }
		}];
	}
    } else {

#else

    {

#endif
	NSUserNotification *notification;
	NSUserNotificationCenter *center;

	center = [NSUserNotificationCenter defaultUserNotificationCenter];
	notification = [[NSUserNotification alloc] init];
	notification.title = title;
	notification.informativeText = detail;
	notification.soundName = NSUserNotificationDefaultSoundName;
	[center setDelegate: (id) self];
	[center deliverNotification:notification];
    }
}

/*
 * Implementation of the NSUserNotificationDelegate protocol.
 */

#if MAC_OS_X_VERSION_MIN_REQUIRED < 110000 || defined(DISABLE_NOTIFICATION_FRAMEWORK)

- (BOOL) userNotificationCenter: (NSUserNotificationCenter *) center
         shouldPresentNotification: (NSUserNotification *)notification
{
    return YES;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
         didDeliverNotification:(NSUserNotification *)notification
{
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
	 didActivateNotification:(NSUserNotification *)notification
{
}

#endif

/*
 * Implementation of the UNUserNotificationDelegate protocol.
 */

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101400

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
         didReceiveNotificationResponse:(UNNotificationResponse *)response
	 withCompletionHandler:(void (^)(void))completionHandler
{
    fprintf(stderr, "Received Notification.\n");
    completionHandler();
}

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
    willPresentNotification:(UNNotification *)notification
    withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler
{
    fprintf(stderr, "Will present notification.\n");
    completionHandler(UNNotificationPresentationOptionNone);
}

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
   openSettingsForNotification:(UNNotification *)notification
{
    fprintf(stderr, "Open notification settings.\n");
}

#endif
@end

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayDestroy --
 *
 * 	Removes an intepreters icon from the status bar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The icon is removed and memory is freed.
 *
 *----------------------------------------------------------------------
 */

static void
MacSystrayDestroy(
    ClientData clientData,
    TCL_UNUSED(Tcl_Interp *))
{
    StatusItemInfo info = (StatusItemInfo)clientData;
    if (info) {
	[*info release];
	ckfree(info);
    }
}

#endif // if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayObjCmd --
 *
 * 	Main command for creating, displaying, and removing icons from the
 * 	status bar.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Management of icon display in the status bar.
 *
 *----------------------------------------------------------------------
 */

static int
MacSystrayObjCmd(
    void *clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj *const *objv)
{
    Tk_Image tk_image;
    StatusItemInfo info = (StatusItemInfo)clientData;
    TkStatusItem *statusItem = *info;
    int result, idx;
    static const char *options[] =
	{"create", "modify", "destroy", NULL};
    typedef enum {TRAY_CREATE, TRAY_MODIFY, TRAY_DESTROY} optionsEnum;
    static const char *modifyOptions[] =
	{"image", "text", "b1_callback", "b3_callback", NULL};
    typedef enum {TRAY_IMAGE, TRAY_TEXT, TRAY_B1_CALLBACK, TRAY_B3_CALLBACK
        } modifyOptionsEnum;

    if ([NSApp macOSVersion] < 101000) {
	Tcl_AppendResult(interp,
	    "StatusItem icons not supported on macOS versions lower than 10.10",
	    NULL);
	return TCL_OK;
    }

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "create | modify | destroy");
	return TCL_ERROR;
    }

    result = Tcl_GetIndexFromObjStruct(interp, objv[1], options,
				       sizeof(char *), "command", 0, &idx);

    if (result != TCL_OK) {
    	return TCL_ERROR;
    }
    switch((optionsEnum)idx) {
    case TRAY_CREATE: {

	if (objc < 3 ||  objc > 6) {
	    Tcl_WrongNumArgs(interp, 1, objv, "create image ?text? ?b1_callback? b3_callback?");
	    return TCL_ERROR;
	}

	if (statusItem == NULL) {
	    statusItem = [[TkStatusItem alloc] init: interp];
	    *info = statusItem;
	} else {
	    Tcl_AppendResult(interp, "Only one system tray icon supported per interpreter", NULL);
	    return TCL_ERROR;
	}

	/*
	 * Create the icon.
	 */

	int width, height;
	Tk_Window tkwin = Tk_MainWindow(interp);
	TkWindow *winPtr = (TkWindow *)tkwin;
	Display *d = winPtr->display;
	NSImage *icon;

	tk_image = Tk_GetImage(interp, tkwin, Tcl_GetString(objv[2]), NULL, NULL);
	if (tk_image == NULL) {
	    return TCL_ERROR;
	}

	Tk_SizeOfImage(tk_image, &width, &height);
	if (width != 0 && height != 0) {
	    icon = TkMacOSXGetNSImageFromTkImage(d, tk_image,
						 width, height);
	    [statusItem setImagewithImage: icon];
	    Tk_FreeImage(tk_image);
	}

	/*
	 * Set the text for the tooltip.
	 */

	NSString *tooltip = [NSString stringWithUTF8String: Tcl_GetString(objv[3])];
	if (tooltip == nil) {
	    Tcl_AppendResult(interp, " unable to set tooltip for systray icon", NULL);
	    return TCL_ERROR;
	}

	[statusItem setTextwithString: tooltip];

	/*
	 * Set the proc for the callback.
	 */

	[statusItem setB1Callback : (objc > 4) ? objv[4] : NULL];
	[statusItem setB3Callback : (objc > 5) ? objv[5] : NULL];
	break;

    }
    case TRAY_MODIFY: {
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 1, objv, "modify object item");
	    return TCL_ERROR;
	}

	/*
	 * Modify the icon.
	 */

	result = Tcl_GetIndexFromObjStruct(interp, objv[2], modifyOptions,
					   sizeof(char *), "option", 0, &idx);

	if (result != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((modifyOptionsEnum)idx) {
	case TRAY_IMAGE: {
	    Tk_Window tkwin = Tk_MainWindow(interp);
	    TkWindow *winPtr = (TkWindow *)tkwin;
	    Display *d = winPtr -> display;
	    NSImage *icon;
	    int width, height;

	    tk_image = Tk_GetImage(interp, tkwin, Tcl_GetString(objv[3]), NULL, NULL);
	    if (tk_image == NULL) {
		Tcl_AppendResult(interp, " unable to obtain image for systray icon", (char * ) NULL);
		return TCL_ERROR;
	    }

	    Tk_SizeOfImage(tk_image, &width, &height);
	    if (width != 0 && height != 0) {
		icon = TkMacOSXGetNSImageFromTkImage(d, tk_image,
						     width, height);
		[statusItem setImagewithImage: icon];
	    }
	    Tk_FreeImage(tk_image);
	    break;
	}

	    /*
	     * Modify the text for the tooltip.
	     */

	case TRAY_TEXT: {
	    NSString *tooltip = [NSString stringWithUTF8String:Tcl_GetString(objv[3])];
	    if (tooltip == nil) {
		Tcl_AppendResult(interp, "unable to set tooltip for systray icon", NULL);
		return TCL_ERROR;
	    }

	    [statusItem setTextwithString: tooltip];
	    break;
	}

	    /*
	     * Modify the proc for the callback.
	     */

	case TRAY_B1_CALLBACK: {
	    [statusItem setB1Callback : objv[3]];
	    break;
	}
	case TRAY_B3_CALLBACK: {
	    [statusItem setB3Callback : objv[3]];
	    break;
	}
    }
    break;
    }

    case TRAY_DESTROY: {
	    /* we don't really distroy, just reset the image, text and callback */
	    [statusItem setImagewithImage: nil];
	    [statusItem setTextwithString: nil];
	    [statusItem setB1Callback : NULL];
	    [statusItem setB3Callback : NULL];
	    break;
	}
    }

#endif //if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000

    return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * SysNotifyObjCmd --
 *
 *      Create system notification.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *      System notifications are posted.
 *
 *-------------------------------z---------------------------------------
 */

static int SysNotifyObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj *const *objv)
{
    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "title message");
	return TCL_ERROR;
    }

    if ([NSApp macOSVersion] < 101000) {
	Tcl_AppendResult(interp,
	    "Notifications not supported on macOS versions lower than 10.10",
	     NULL);
	return TCL_OK;
    }

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000

    NSString *title = [NSString stringWithUTF8String: Tcl_GetString(objv[1])];
    NSString *message = [NSString stringWithUTF8String: Tcl_GetString(objv[2])];
    [notifier postNotificationWithTitle : title message: message];

#endif
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayInit --
 *
 * 	Initialize this package and create script-level commands.
 *      This is called from TkpInit for each interpreter.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	The tk systray and tk sysnotify commands are installed in an
 *	interpreter
 *
 *----------------------------------------------------------------------
 */

int
MacSystrayInit(Tcl_Interp *interp)
{

    /*
     * Initialize the TkStatusItem for this interpreter, and the shared
     * TkUserNotifier, if it has not been initialized yet.
     */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000

    StatusItemInfo info = (StatusItemInfo) ckalloc(sizeof(StatusItemInfo));
    *info = 0;
    if (notifier == nil) {
	notifier = [[TkUserNotifier alloc] init];
    }

#endif

    if (@available(macOS 10.14, *)) {
	UNUserNotificationCenter *center;
	UNNotificationCategory *category;
	NSSet *categories;

	TkNotificationCategory = @"Basic Tk Notification";
	center = [UNUserNotificationCenter currentNotificationCenter];
	category = [UNNotificationCategory
		       categoryWithIdentifier:TkNotificationCategory
		       actions:@[]
		       intentIdentifiers:@[]
		       options: UNNotificationCategoryOptionNone];
	categories = [NSSet setWithObjects:category, nil];
	[center setNotificationCategories: categories];
    }

    Tcl_CreateObjCommand(interp, "_systray", MacSystrayObjCmd, info, NULL);
    Tcl_CreateObjCommand(interp, "_sysnotify", SysNotifyObjCmd, NULL, NULL);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
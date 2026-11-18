#ifndef IOS_APP_BRANDING_H
#define IOS_APP_BRANDING_H

#define IOS_APP_DISPLAY_NAME "SHITBOX LABZ INC."

#ifdef __OBJC__
#import <Foundation/Foundation.h>

static inline NSString *ios_app_display_name(void) {
    return [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDisplayName"] ?: @IOS_APP_DISPLAY_NAME;
}

static inline NSString *ios_app_release_version(void) {
    NSBundle *bundle = NSBundle.mainBundle;
    return [bundle objectForInfoDictionaryKey:@"ShitboxReleaseVersion"]
        ?: [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"Unknown";
}
#endif

#endif

#include "motu_prefs.h"

#import <Foundation/Foundation.h>

namespace motu::prefs {
namespace {

std::string decodeName(id value) {
    if ([value isKindOfClass:[NSString class]])
        return [(NSString*)value UTF8String] ?: "";
    if (![value isKindOfClass:[NSData class]] || [(NSData*)value length] == 0)
        return {};
    NSData* d = (NSData*)value;
    // A BOM selects the byte order; NSUTF16StringEncoding honours it.
    NSString* s = [[NSString alloc] initWithData:d encoding:NSUTF16StringEncoding];
    if (!s) return {};
    return [s UTF8String] ?: "";
}

std::vector<std::string> decodeArray(id value) {
    std::vector<std::string> out;
    if (![value isKindOfClass:[NSArray class]]) return out;
    for (id item in (NSArray*)value) out.push_back(decodeName(item));
    return out;
}

}  // namespace

bool readChannelNames(const std::string& path, ChannelNames& out, std::string* err) {
    @autoreleasepool {
        NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:@(path.c_str())];
        if (!d) {
            if (err) *err = "Could not read " + path;
            return false;
        }
        out.inputs = decodeArray(d[@"InputNames"]);
        out.outputs = decodeArray(d[@"OutputNames"]);
        if (out.inputs.empty() && out.outputs.empty()) {
            if (err) *err = "No channel names in " + path;
            return false;
        }
        return true;
    }
}

std::vector<std::string> findPrefsFiles() {
    @autoreleasepool {
        NSFileManager* fm = [NSFileManager defaultManager];
        NSMutableArray<NSString*>* homes = [NSMutableArray arrayWithObject:NSHomeDirectory()];
        for (NSString* vol in [fm contentsOfDirectoryAtPath:@"/Volumes" error:nil]) {
            NSString* users = [@"/Volumes" stringByAppendingPathComponent:vol];
            users = [users stringByAppendingPathComponent:@"Users"];
            for (NSString* u in [fm contentsOfDirectoryAtPath:users error:nil])
                [homes addObject:[users stringByAppendingPathComponent:u]];
        }

        std::vector<std::pair<NSDate*, std::string>> found;
        for (NSString* home in homes) {
            NSString* dir = [home stringByAppendingPathComponent:@"Library/Preferences/com.motu.PCIAudio"];
            for (NSString* f in [fm contentsOfDirectoryAtPath:dir error:nil]) {
                // PCI-424.bus19.slot0.plist, not its .CueMix / .smux siblings.
                if (![f hasPrefix:@"PCI-424."] || ![f hasSuffix:@".slot0.plist"]) continue;
                NSString* p = [[dir stringByAppendingPathComponent:f] stringByResolvingSymlinksInPath];
                NSDate* m = [[fm attributesOfItemAtPath:p error:nil] fileModificationDate] ?: [NSDate distantPast];
                std::string s = [p UTF8String];
                bool dup = false;
                for (auto& e : found) dup |= e.second == s;
                if (!dup) found.emplace_back(m, s);
            }
        }
        std::sort(found.begin(), found.end(),
                  [](auto& a, auto& b) { return [a.first compare:b.first] == NSOrderedDescending; });
        std::vector<std::string> out;
        for (auto& e : found) out.push_back(e.second);
        return out;
    }
}

}  // namespace motu::prefs

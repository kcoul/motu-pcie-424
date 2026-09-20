// mkicon — render original app icons with CoreGraphics.
//
// Replaces MOTU's own icons, which cannot ship in a public repo. Two icons in
// one family: a dark rounded tile with a gold PCI edge connector along the
// bottom, and a motif above that says which app it is.
//
//   setup   three routing lanes with nodes  -- configuration
//   cuemix  four faders at different levels -- mixing
//
// Both are built from two or three bold shapes so they survive 16 px.
//
//   clang -fobjc-arc -framework Foundation -framework CoreGraphics \
//         -framework ImageIO -framework CoreServices -o mkicon mkicon.m

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

static const CGFloat S = 1024.0;

static void roundRect(CGContextRef c, CGRect r, CGFloat rad) {
    CGContextBeginPath(c);
    CGContextMoveToPoint(c, CGRectGetMinX(r) + rad, CGRectGetMinY(r));
    CGContextAddArcToPoint(c, CGRectGetMaxX(r), CGRectGetMinY(r), CGRectGetMaxX(r), CGRectGetMaxY(r), rad);
    CGContextAddArcToPoint(c, CGRectGetMaxX(r), CGRectGetMaxY(r), CGRectGetMinX(r), CGRectGetMaxY(r), rad);
    CGContextAddArcToPoint(c, CGRectGetMinX(r), CGRectGetMaxY(r), CGRectGetMinX(r), CGRectGetMinY(r), rad);
    CGContextAddArcToPoint(c, CGRectGetMinX(r), CGRectGetMinY(r), CGRectGetMaxX(r), CGRectGetMinY(r), rad);
    CGContextClosePath(c);
}

static void fillRound(CGContextRef c, CGRect r, CGFloat rad, CGFloat rr, CGFloat gg, CGFloat bb, CGFloat aa) {
    CGContextSetRGBFillColor(c, rr, gg, bb, aa);
    roundRect(c, r, rad);
    CGContextFillPath(c);
}

// The shared tile: a vertical gradient from slate to near-black, a hairline
// inner border, and the gold edge connector every PCI card has.
static void background(CGContextRef c) {
    CGRect tile = CGRectMake(S * 0.055, S * 0.055, S * 0.89, S * 0.89);
    CGFloat rad = S * 0.20;

    CGContextSaveGState(c);
    roundRect(c, tile, rad);
    CGContextClip(c);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGFloat comps[] = { 0.180, 0.208, 0.243, 1.0,    // slate
                        0.075, 0.086, 0.106, 1.0 };  // near-black
    CGFloat locs[] = { 0.0, 1.0 };
    CGGradientRef grad = CGGradientCreateWithColorComponents(cs, comps, locs, 2);
    CGContextDrawLinearGradient(c, grad, CGPointMake(0, S), CGPointMake(0, 0), 0);
    CGGradientRelease(grad);
    CGColorSpaceRelease(cs);

    // Gold edge fingers, clipped to the tile so they meet its bottom corners.
    CGFloat fy = S * 0.055, fh = S * 0.10;
    CGContextSetRGBFillColor(c, 0.839, 0.678, 0.314, 1.0);
    CGContextFillRect(c, CGRectMake(S * 0.055, fy, S * 0.89, fh));
    // Notches between the contacts.
    CGContextSetRGBFillColor(c, 0.075, 0.086, 0.106, 1.0);
    for (int i = 0; i < 9; ++i) {
        CGFloat x = S * 0.125 + i * S * 0.088;
        CGContextFillRect(c, CGRectMake(x, fy, S * 0.022, fh * 0.72));
    }
    CGContextRestoreGState(c);

    // Inner hairline, drawn after the clip so it sits on top.
    CGContextSetRGBStrokeColor(c, 1.0, 1.0, 1.0, 0.10);
    CGContextSetLineWidth(c, S * 0.006);
    roundRect(c, CGRectInset(tile, S * 0.010, S * 0.010), rad - S * 0.010);
    CGContextStrokePath(c);
}

// Setup: three routing lanes, each with a node, offset to read as a signal path.
static void setupMotif(CGContextRef c) {
    CGFloat x0 = S * 0.175, w = S * 0.65, h = S * 0.052, rad = h / 2;
    CGFloat ys[] = { S * 0.615, S * 0.470, S * 0.325 };
    CGFloat nodeAt[] = { 0.70, 0.38, 0.58 };
    for (int i = 0; i < 3; ++i) {
        fillRound(c, CGRectMake(x0, ys[i], w, h), rad, 1.0, 1.0, 1.0, 0.22);
        CGFloat nx = x0 + w * nodeAt[i];
        fillRound(c, CGRectMake(x0, ys[i], nx - x0, h), rad, 0.325, 0.722, 0.949, 1.0);
        CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
        CGContextFillEllipseInRect(c, CGRectMake(nx - h * 0.72, ys[i] - h * 0.22, h * 1.44, h * 1.44));
    }
}

// CueMix: four faders at different levels, caps highlighted.
static void cuemixMotif(CGContextRef c) {
    CGFloat trackW = S * 0.052, capH = S * 0.055;
    CGFloat top = S * 0.660, bot = S * 0.300, span = top - bot;
    CGFloat level[] = { 0.72, 0.35, 0.90, 0.52 };
    for (int i = 0; i < 4; ++i) {
        CGFloat x = S * 0.215 + i * S * 0.165;
        fillRound(c, CGRectMake(x, bot, trackW, span), trackW / 2, 1.0, 1.0, 1.0, 0.20);
        CGFloat cy = bot + span * level[i];
        fillRound(c, CGRectMake(x, bot, trackW, cy - bot), trackW / 2, 0.325, 0.722, 0.949, 1.0);
        fillRound(c, CGRectMake(x - trackW * 0.44, cy - capH / 2, trackW * 1.88, capH),
                  capH * 0.32, 0.965, 0.969, 0.976, 1.0);
    }
}

static void writeIcon(const char* path, void (*motif)(CGContextRef)) {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate(NULL, (size_t)S, (size_t)S, 8, 0, cs,
                                           kCGImageAlphaPremultipliedLast);
    CGContextSetAllowsAntialiasing(c, true);
    CGContextSetShouldAntialias(c, true);
    background(c);
    motif(c);

    CGImageRef img = CGBitmapContextCreateImage(c);
    CFStringRef p = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, p, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef d = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    CGImageDestinationAddImage(d, img, NULL);
    CGImageDestinationFinalize(d);
    CFRelease(d); CFRelease(url); CFRelease(p);
    CGImageRelease(img); CGContextRelease(c); CGColorSpaceRelease(cs);
    fprintf(stderr, "wrote %s\n", path);
}

int main(int argc, const char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: mkicon <setup.png> <cuemix.png>\n"); return 2; }
    writeIcon(argv[1], setupMotif);
    writeIcon(argv[2], cuemixMotif);
    return 0;
}

/*
 * Copyright (c) 2013-14 Mikko Mononen memon@inside.org
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * The SVG parser is based on Anti-Grain Geometry 2.4 SVG example
 * Copyright (C) 2002-2004 Maxim Shemanarev (McSeem) (http://www.antigrain.com/)
 *
 * Arc calculation code based on canvg (https://code.google.com/p/canvg/)
 *
 * Bounding box calculation based on http://blog.hackers-cafe.net/2009/06/how-to-calculate-bezier-curves-bounding.html
 *
 */

#ifndef NANOSVG_H
#define NANOSVG_H

#ifndef NANOSVG_CPLUSPLUS
#ifdef __cplusplus
extern "C" {
#endif
#endif

// NanoSVG is a simple stupid single-header-file SVG parse. The output of the parser is a list of cubic bezier shapes.
//
// The library suits well for anything from rendering scalable icons in your editor application to prototyping a game.
//
// NanoSVG supports a wide range of SVG features, but something may be missing, feel free to create a pull request!
//
// The shapes in the SVG images are transformed by the viewBox and converted to specified units.
// That is, you don't necessarily need to know what the viewBox or units are used in the SVG,
// you can just render the returned shapes.
//
// Important note: by default all polygons are filled. If you want to render strokes, you need to enable it
// at parse time by calling nsvgSetParserFlag(p, NSVG_FLAG_STROKE, 1);
//
// Supported SVG elements:
// - path
// - polyline
// - polygon
// - rect
// - circle
// - ellipse
// - line
// - image
// - defs
// - use
// - g (group)
// - symbol
//
// Supported attributes:
// - id
// - class
// - transform
// - display
// - overflow
// - viewBox
// - width, height
// - preserveAspectRatio
// - stroke-width
// - stroke-linecap
// - stroke-linejoin
// - stroke-opacity
// - stroke-dasharray
// - stroke-dashoffset
// - stroke-miterlimit
// - stroke
// - stroke-gradient
// - fill-opacity
// - fill
// - fill-gradient
// - fill-rule
// - clip-path
// - clip-rule
// - opacity
// - stop-color
// - stop-opacity
// - style
// - visibility
// - font-size
// - font-family
// - font-weight
// - font-style
// - text-anchor
// - mix-blend-mode
//
// Supported SVG features:
// - Basic shapes
// - Paths
// - Gradients
// - Patterns
// - Clipping paths
// - Masks
// - Rounded rects
// - Elliptical arcs
// - Dashed strokes
// - Inner and outer reference for gradient

/* Example usage:
	// Load SVG
	NSVGimage* image;
	image = nsvgParseFromFile("test.svg", "px", 96);
	printf("size: %f x %f\n", image->width, image->height);
	// Use...
	for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
		for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
			for (int i = 0; i < path->npts-1; i += 3) {
				float* p = &path->pts[i*2];
				drawCubicBez(p[0],p[1], p[2],p[3], p[4],p[5], p[6],p[7]);
			}
		}
	}
	// Delete
	nsvgDelete(image);
*/

enum NSVGpaintType {
	NSVG_PAINT_NONE = 0,
	NSVG_PAINT_COLOR = 1,
	NSVG_PAINT_LINEAR_GRADIENT = 2,
	NSVG_PAINT_RADIAL_GRADIENT = 3,
	NSVG_PAINT_PATTERN = 4
};

enum NSVGspreadType {
	NSVG_SPREAD_PAD = 0,
	NSVG_SPREAD_REFLECT = 1,
	NSVG_SPREAD_REPEAT = 2
};

enum NSVGlineJoin {
	NSVG_JOIN_MITER = 0,
	NSVG_JOIN_ROUND = 1,
	NSVG_JOIN_BEVEL = 2
};

enum NSVGlineCap {
	NSVG_CAP_BUTT = 0,
	NSVG_CAP_ROUND = 1,
	NSVG_CAP_SQUARE = 2
};

enum NSVGfillRule {
	NSVG_FILLRULE_NONZERO = 0,
	NSVG_FILLRULE_EVENODD = 1
};

enum NSVGflags {
	NSVG_FLAGS_VISIBLE = 0x01
};

enum NSVGparserFlags {
	NSVG_FLAG_VISIBLE = 0x01,
	NSVG_FLAG_FILL = 0x02,
	NSVG_FLAG_STROKE = 0x04,
	NSVG_FLAG_CLIP = 0x08,
	NSVG_FLAG_DISCARD = 0x10,
	NSVG_FLAG_SYSTEM = 0x20
};

enum NSVGblendMode {
	NSVG_BLEND_NORMAL = 0,
	NSVG_BLEND_DARKEN,
	NSVG_BLEND_MULTIPLY,
	NSVG_BLEND_COLORBURN,
	NSVG_BLEND_LIGHTEN,
	NSVG_BLEND_SCREEN,
	NSVG_BLEND_COLORDODGE,
	NSVG_BLEND_OVERLAY,
	NSVG_BLEND_SOFTLIGHT,
	NSVG_BLEND_HARDLIGHT,
	NSVG_BLEND_DIFFERENCE,
	NSVG_BLEND_EXCLUSION,
	NSVG_BLEND_HUE,
	NSVG_BLEND_SATURATION,
	NSVG_BLEND_COLOR,
	NSVG_BLEND_LUMINOSITY
};

enum NSVGpathMethods {
	NSVG_PATH_MOVETO,
	NSVG_PATH_LINETO,
	NSVG_PATH_CURVETO,
	NSVG_PATH_CLOSE
};

typedef struct NSVGgradientStop {
	unsigned int color;
	float offset;
} NSVGgradientStop;

typedef struct NSVGgradient {
	float xform[6];
	char spread;
	float fx, fy;
	int nstops;
	NSVGgradientStop stops[1];
} NSVGgradient;

typedef struct NSVGpattern {
	float xform[6];
	float width;
	float height;
	char href[256];
	char units;
} NSVGpattern;

typedef struct NSVGpaint {
	char type;
	union {
		unsigned int color;
		NSVGgradient* gradient;
		NSVGpattern* pattern;
	};
} NSVGpaint;

typedef struct NSVGpath
{
	float* pts;					// Cubic bezier points: x0,y0, [cpx1,cpx1,cpx2,cpy2,x1,y1], ...
	int npts;					// Total number of bezier points.
	char closed;				// Flag indicating if shapes should be treated as closed.
	float bounds[4];			// Tight bounding box of the shape [minx,miny,maxx,maxy].
	struct NSVGpath* next;		// Pointer to next path, or NULL if last element.
} NSVGpath;

typedef struct NSVGshape
{
	char id[64];				// Optional 'id' attr of the shape or its group
	NSVGpaint fill;				// Fill paint
	NSVGpaint stroke;			// Stroke paint
	float opacity;				// Opacity of the shape.
	float strokeWidth;			// Stroke width (scaled).
	float strokeDashOffset;		// Stroke dash offset (scaled).
	float strokeDashArray[8];	// Stroke dash array (scaled).
	char strokeDashCount;		// Number of dash values in dash array.
	char strokeLineJoin;		// Stroke join type.
	char strokeLineCap;			// Stroke cap type.
	float miterLimit;			// Miter limit
	char fillRule;				// Fill rule, see NSVGfillRule.
	unsigned char flags;		// Logical or of NSVG_FLAGS_* flags
	float bounds[4];			// Tight bounding box of the shape [minx,miny,maxx,maxy].
	char blendMode;				// The blend mode of the shape. Default is normal.
	char clip;					// If 1, the shape is a clipping path
	char hasMask;				// If the shape is clipped by a mask
	NSVGpath* paths;			// Linked list of paths in the image.
	struct NSVGshape* next;		// Pointer to next shape, or NULL if last element.
} NSVGshape;

typedef struct NSVGimage
{
	float width;				// Width of the image.
	float height;				// Height of the image.
	NSVGshape* shapes;			// Linked list of shapes in the image.
} NSVGimage;

typedef struct NSVGpositionData
{
	float* xform;
	float x;
	float y;
	float degrees;
	float scaleX;
	float scaleY;
} NSVGpositionData;

// Parses SVG file from a file, returns SVG image as paths.
NSVGimage* nsvgParseFromFile(const char* filename, const char* units, float dpi);

// Parses SVG file from a null terminated string, returns SVG image as paths.
// Important note: changes the string.
NSVGimage* nsvgParse(char* input, const char* units, float dpi);

// Parses SVG file from a file, returns SVG image as paths.
NSVGimage* nsvgParseFromFileWithOptions(const char* filename, const char* units, float dpi, unsigned int flags);

// Parses SVG file from a null terminated string, returns SVG image as paths.
// Important note: changes the string.
NSVGimage* nsvgParseWithOptions(char* input, const char* units, float dpi, unsigned int flags);

// Duplicates a path.
NSVGpath* nsvgDuplicatePath(NSVGpath* p);

// Resolves positioning data for the shape based on the transform chain
void nsvgShapePosition(NSVGshape* shape, NSVGpositionData* data);

// Deletes an image.
void nsvgDelete(NSVGimage* image);

// Applies the scale to image
NSVGimage* nsvgScale(NSVGimage* image, float scale);

// Searches a style property from all styles applicable to the given node (ordered lowest to highest priority)
// For example, considers styles specified in the element and also the CSS cascading rules.
// Returns true if the property is found, false otherwise
int nsvgGetPropertyFromStyle(const char* style, const char* name, char* value, int maxlen);

// Searches a style property in element attributes (as an inline style, not in a styles section)
// Returns true if the property is found, false otherwise
int nsvgGetProperty(const char* node, const char* name, char* value, int maxlen);

// Searches a special property in element attributes
// Returns true if the property is found, false otherwise
int nsvgGetSpecialProperty(const char* node, const char* name, char* value, int maxlen);

// Creates a new parser and returns a handle to it
void* nsvgCreateParser();

// Sets a parser flag
void nsvgSetParserFlag(void* parser, int flag, int value);

// Parse SVG file at filename (with units in the specified string and at the specified DPI), returning the resulting image
NSVGimage* nsvgParseFromFileWithParser(void* parser, const char* filename, const char* units, float dpi);

// Parse the SVG contained in input (with units in the specified string and at the specified DPI), returning the resulting image
NSVGimage* nsvgParseWithParser(void* parser, char* input, const char* units, float dpi);

// Delete a parser
void nsvgDeleteParser(void* parser);

// Output the current parser state as an image
NSVGimage* nsvgImageFromParser(void* parser);

// Convert the image to RGBA
// Uses the specified fill rule, scaling, and transforms
// Returns a vector of bytes, allocated by the function
// (4 bytes per pixel, width * height pixels)
unsigned char* nsvgCreateRasterizer();
void nsvgRasterize(unsigned char* r, NSVGimage* image, float tx, float ty, float scale, unsigned char* dst, int w, int h, int stride);
void nsvgDeleteRasterizer(unsigned char* r);

#ifndef NANOSVG_CPLUSPLUS
#ifdef __cplusplus
}
#endif
#endif

#endif // NANOSVG_H

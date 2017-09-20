/* ImageMagick Graphics Device
 * R graphics docs: https://svn.r-project.org/R/trunk/src/include/R_ext/GraphicsDevice.h
 * Magick++ docs: https://www.imagemagick.org/Magick++/Drawable.html
 *
 * Constants are defined in https://svn.r-project.org/R/trunk/src/include/R_ext/GraphicsEngine.h
 * and https://www.imagemagick.org/Magick++/Enumerations.html
 *
 */
#include "magick_types.h"
#include <R_ext/GraphicsEngine.h>
#define pi 3.14159265359

// Magick Device Parameters
class MagickDevice {
public:
  XPtrImage ptr;
  bool drawing;
  bool antialias;
  double clipleft, clipright, cliptop, clipbottom;
  MagickDevice(bool drawing_, bool antialias_):
    ptr(XPtrImage(new Image())),
    drawing(drawing_),
    antialias(antialias_),
    clipleft(0), clipright(0), cliptop(0), clipbottom(0){}
  MagickDevice(bool drawing_, bool antialias_, Image * image):
    ptr(XPtrImage(image)),
    drawing(drawing_),
    antialias(antialias_),
    clipleft(0), clipright(0), cliptop(0), clipbottom(0){}
};

// Get the 'latest' device
static MagickDevice * dirty = NULL;

//from 'svglite' source: 1 lwd = 1/96", but units in rest of document are 1/72"
#define xlwd (72.0/96.0)

typedef std::container<Magick::Drawable> drawlist;
typedef std::container<Magick::Coordinate> coordlist;
typedef std::container<Magick::VPath> pathlist;

static inline bool same(double x, double y){
  return std::abs(x - y) < 0.5;
}

static inline MagickDevice * getdev(pDevDesc dd){
  MagickDevice * device = (MagickDevice *) dd->deviceSpecific;
  if(device == NULL)
    throw std::runtime_error("Graphics device pointing to NULL image");
  return device;
}

static inline XPtrImage getptr(pDevDesc dd){
  return getdev(dd)->ptr;
}

static inline Image * getimage(pDevDesc dd){
  return getptr(dd).get();
}

static inline Frame * getgraph(pDevDesc dd){
  Image * image = getimage(dd);
  if(image->size() < 1)
    throw std::runtime_error("Magick device has zero pages");
  return &image->back();
}

/* from svglite */
static inline bool is_bold(int face) {
  return face == 2 || face == 4;
}

static inline bool is_italic(int face) {
  return face == 3 || face == 4;
}

static inline bool is_symbol(int face) {
  return face == 5;
}

static inline double scale_lty(int lty, double lwd) {
  return ((lwd > 1) ? lwd : 1) * (lty & 15);
}

/* From: https://github.com/wch/r-source/blob/tags/R-3-4-1/src/include/R_ext/GraphicsEngine.h#L342-L370 */
static inline double * linetype(double * out, int lty, int lwd){
  switch (lty) {
  case LTY_BLANK:
  case LTY_SOLID:
    out[1] = 1;
    break;
  default:
    out[0] = scale_lty(lty, lwd);
    lty = lty >> 4;
    for(int i = 1 ; i < 8 && lty & 15; i++) {
      out[i] = scale_lty(lty, lwd);
      lty = lty >> 4;
    }
    break;
  }
  return out;
}

static inline Magick::DrawableStrokeLineCap linecap(R_GE_lineend type){
  switch(type){
  case GE_ROUND_CAP:
    return Magick::RoundCap;
  case GE_BUTT_CAP:
    return Magick::ButtCap;
  case GE_SQUARE_CAP:
    return Magick::SquareCap;
  }
  return Magick::RoundCap;
}

static inline Magick::DrawableStrokeLineJoin linejoin(R_GE_linejoin type){
  switch(type){
  case GE_ROUND_JOIN:
    return Magick::RoundJoin;
  case GE_MITRE_JOIN:
    return Magick::MiterJoin;
  case GE_BEVEL_JOIN:
    return Magick::BevelJoin;
  }
  return Magick::RoundJoin;
}

static inline Magick::StyleType style(int face){
  return is_italic(face) ? Magick::ItalicStyle : Magick::NormalStyle;
}

static inline int weight(int face){
  return is_bold(face) ? 700 : 400;
}

static inline std::string fontname(const pGEcontext gc){
  if(is_symbol(gc->fontface))
    return std::string("symbol");
  if(!strlen(gc->fontfamily))
    return std::string("sans-serif");
  if(!strncmp(gc->fontfamily, "sans", 4) || !strncmp(gc->fontfamily, "Sans", 4))
    return std::string("sans-serif");
  if(!strncmp(gc->fontfamily, "mono", 4) || !strncmp(gc->fontfamily, "Mono", 4))
    return std::string("monospace");
  return std::string(gc->fontfamily);
}

static inline coordlist coord(int n, double * x, double * y){
  coordlist coordinates;
  for(int i = 0; i < n; i++)
    coordinates.push_back(Magick::Coordinate(x[i], y[i]));
  return coordinates;
}

/* main drawing function */
static void image_draw(drawlist x, const pGEcontext gc, pDevDesc dd, bool join = true, bool fill = true){
  double multiplier = 1/dd->ipr[0]/72;
  double lwd = gc->lwd * xlwd * multiplier;
  double lty[10] = {0};
  drawlist draw;
  if(gc->col != NA_INTEGER)
    draw.push_back(Magick::DrawableStrokeColor(Color(col2name(gc->col))));
  if(fill == true && gc->fill != NA_INTEGER)
    draw.push_back(Magick::DrawableFillColor(Color(col2name(gc->fill))));
  draw.push_back(Magick::DrawableStrokeWidth(lwd));
  draw.push_back(Magick::DrawableStrokeLineCap(linecap(gc->lend)));
  draw.push_back(Magick::DrawableStrokeAntialias(getdev(dd)->antialias));
  if(join == true)
    draw.push_back(Magick::DrawableStrokeLineJoin(linejoin(gc->ljoin)));
  draw.push_back(Magick::DrawableMiterLimit(gc->lmitre * multiplier));
  draw.push_back(Magick::myDrawableDashArray(linetype(lty, gc->lty, lwd)));
  for ( drawlist::iterator it = x.begin(); it!= x.end(); ++it )
    draw.push_back(*it);
  if(getdev(dd)->drawing){
    Image * image = getimage(dd);
    for_each (image->begin(), image->end(), Magick::drawImage(draw));
    for_each (image->begin(), image->end(), Magick::gammaImage(gc->gamma));
  } else {
    Frame * graph = getgraph(dd);
    graph->draw(draw);
    graph->gamma(gc->gamma);
  }
}

static void image_draw(Magick::Drawable x, const pGEcontext gc, pDevDesc dd, bool join = true, bool fill = true){
  drawlist draw;
  draw.push_back(x);
  image_draw(draw, gc, dd, join, fill);
}

/* ~~~ CALLBACK FUNCTIONS START HERE ~~~ */

static void image_new_page(const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  Image *image = getimage(dd);
  if(image->size() > 0 && getdev(dd)->drawing)
    throw std::runtime_error("Cannot open a new page on a drawing device");
  Frame x(Geom(dd->right, dd->bottom), Color(col2name(gc->fill)));
  x.magick("PNG");
  x.depth(16L);
  x.strokeAntiAlias(getdev(dd)->antialias);
  x.myAntiAlias(getdev(dd)->antialias);
  image->push_back(x);
  VOID_END_RCPP
}

/* TODO: test if we got the coordinates right  */
/* TODO: how to unset the clipmask ? */
static void image_clip(double left, double right, double bottom, double top, pDevDesc dd) {
  if(!dd->canClip)
    return;

  //avoid duplications
  MagickDevice * dev = getdev(dd);
  if(same(dev->clipleft, left) && same(dev->clipright, right) && same(dev->clipbottom, bottom) && same(dev->cliptop, top))
    return;
  dev->clipleft = left;
  dev->clipright = right;
  dev->clipbottom = bottom;
  dev->cliptop = top;

  BEGIN_RCPP
  pathlist path;
  path.push_back(Magick::PathMovetoAbs(Magick::Coordinate(left + 1, top + 1)));
  path.push_back(Magick::PathLinetoAbs(Magick::Coordinate(right - 1, top + 1)));
  path.push_back(Magick::PathLinetoAbs(Magick::Coordinate(right - 1, bottom - 1)));
  path.push_back(Magick::PathLinetoAbs(Magick::Coordinate(left + 1, bottom - 1)));
  path.push_back(Magick::PathLinetoAbs(Magick::Coordinate(left + 1, top + 1)));

  drawlist draw;
  std::string id("mypath");
  draw.push_back(Magick::DrawablePushClipPath(id));
  draw.push_back(Magick::DrawablePath(path));
  draw.push_back(Magick::DrawablePopClipPath());
  draw.push_back(Magick::DrawableClipPath(id));
  if(getdev(dd)->drawing){
    Image * image = getimage(dd);
    for_each (image->begin(), image->end(), Magick::drawImage(draw));
  } else {
    Frame * graph = getgraph(dd);
    graph->draw(draw);
  }
  VOID_END_RCPP
}

static void image_line(double x1, double y1, double x2, double y2, const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  image_draw(Magick::DrawableLine(x1, y1, x2, y2), gc, dd);
  VOID_END_RCPP
}

static void image_polyline(int n, double *x, double *y, const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  drawlist draw;
  //Note 'fill' must be unset to prevent magick from creating a polygon
  draw.push_back(Magick::DrawableFillColor(Magick::Color()));
  //workaround for issue #60
#if MagickLibVersion < 0x697
  bool join = false;
#else
  bool join = true;
#endif
  draw.push_back(Magick::DrawablePolyline(coord(n, x, y)));
  image_draw(draw, gc, dd, join, false);
  VOID_END_RCPP
}

static void image_polygon(int n, double *x, double *y, const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  image_draw(Magick::DrawablePolygon(coord(n, x, y)), gc, dd);
  VOID_END_RCPP
}

static void image_rect(double x0, double y0, double x1, double y1,
                const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  image_draw(Magick::DrawableRectangle(x0, y1, x1, y0), gc, dd);
  VOID_END_RCPP
}

static void image_circle(double x, double y, double r, const pGEcontext gc,
                  pDevDesc dd) {
  BEGIN_RCPP
  //note: parameter 3 + 4 must denote any point on the circle
  image_draw(Magick::DrawableCircle(x, y, x, y + r), gc, dd);
  VOID_END_RCPP
}

static void image_path(double *x, double *y, int npoly, int *nper, Rboolean winding,
              const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  getgraph(dd)->fillRule(winding ? Magick::NonZeroRule : Magick::EvenOddRule);
  pathlist path;
  for (int i = 0; i < npoly; i++) {
    int n = nper[i];
    path.push_back(Magick::PathMovetoAbs(Magick::Coordinate(x[0], y[0])));
    for(int j = 1; j < n; j++){
      path.push_back(Magick::PathLinetoAbs(Magick::Coordinate(x[j], y[j])));
    }
    path.push_back(Magick::PathLinetoAbs(Magick::Coordinate(x[0], y[0])));
    x+=n;
    y+=n;
  }
  image_draw(Magick::DrawablePath(path), gc, dd);
  VOID_END_RCPP
}

static void image_size(double *left, double *right, double *bottom, double *top,
              pDevDesc dd) {
  *left = dd->left;
  *right = dd->right;
  *bottom = dd->bottom;
  *top = dd->top;
}

/* TODO: we rotate around centerpoint whereas png() rotates around (x,y) */
static void image_raster(unsigned int *raster, int w, int h,
                double x, double y,
                double width, double height,
                double rot,
                Rboolean interpolate,
                const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  //normalize
  rot = fmod(-rot + 360.0, 360.0);
  height = - height;

  //create the raster frame
  Frame frame(w, h, std::string("RGBA"), Magick::CharPixel, raster);
  frame.backgroundColor(Color("transparent"));
  Magick::Geometry size = Geom(width, height);
  size.aspect(true); //resize without preserving aspect ratio
  frame.filterType(interpolate ? Magick::TriangleFilter : Magick::PointFilter);
  frame.resize(size);

  //rotate minimum 1 degree. Adjust positioning to rotate around (x,y)
  if(rot > 1){
    frame.rotate(rot);
    double rad = (rot * pi) / 180;
    x += round(width * fmin(0.0, cos(rad)) + height * fmin(0.0, sin(rad)));
    y -= round(height * fmin(0.0, cos(rad)) + width * fmin(0.0, -sin(rad)));

    //calculate new values
    Magick::Geometry outsize(frame.size());
    width = outsize.width();
    height = outsize.height();
  }

  Magick::DrawableCompositeImage draw(x, y - height, width, height, frame, Magick::OverCompositeOp);
  image_draw(draw, gc, dd);
  VOID_END_RCPP
}

/* TODO: somehow R adds another protect */
static void image_close(pDevDesc dd) {
  BEGIN_RCPP
  dirty = NULL;
  XPtrImage ptr = getptr(dd);
  MagickDevice * device = (MagickDevice *) dd->deviceSpecific;
  delete device;
  VOID_END_RCPP
}

SEXP image_capture(pDevDesc dd){
  BEGIN_RCPP
  Frame * graph = getgraph(dd);
  Rcpp::IntegerMatrix out(dd->bottom, dd->right);
  Magick::Blob output;
  graph->write(&output, "rgba", 8L);
  std::memcpy(out.begin(), output.data(), output.length());
  return out;
  VOID_END_RCPP
  return R_NilValue;
}

void image_mode(int mode, pDevDesc dd){
  if(!mode){
    dirty = getdev(dd);
  }
}

static inline Magick::DrawableAffine RotateDrawing(double deg, double x, double y){
  double rad = (deg * pi) / 180;
  double sx = cos(rad);
  double sy = cos(rad);
  double rx = sin(rad);
  double ry = -sin(rad);
  double tx = x + x * sx + y * rx;
  double ty = y + y * sy + x * ry;
  return Magick::DrawableAffine(sx, sy, rx, ry, tx, ty);
}

static void image_text(double x, double y, const char *str, double rot,
                double hadj, const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  double multiplier = 1/dd->ipr[0]/72;
  double deg = fmod(-rot + 360.0, 360.0);
  double ps = gc->ps * gc->cex * multiplier;

  /* text color */
  Magick::Color fill(col2name(gc->col));
  Magick::Color stroke;

  /* there is a bug in IM that prefers these properties over the draw list ones */
  Frame * graph = getgraph(dd);
  graph->fontPointsize(ps);
  graph->strokeColor(stroke);
  graph->fillColor(fill);
#if MagickLibVersion >= 0x692
  graph->fontFamily(fontname(gc));
  graph->fontWeight(weight(gc->fontface));
  graph->fontStyle(style(gc->fontface));
#endif

  drawlist draw;
  draw.push_back(Magick::DrawableStrokeColor(stroke));
  draw.push_back(Magick::DrawableFillColor(fill));
  draw.push_back(Magick::DrawableFont(fontname(gc), style(gc->fontface), weight(gc->fontface), Magick::NormalStretch));
  draw.push_back(Magick::DrawablePointSize(ps));
  draw.push_back(Magick::DrawableTextAntialias(getdev(dd)->antialias));
  draw.push_back(Magick::DrawableText(x, y, std::string(str), "UTF-8"));
  if(deg > 1)
    draw.push_back(RotateDrawing(deg, x, y));
  image_draw(draw, gc, dd);
  VOID_END_RCPP
}

static void image_metric_info(int c, const pGEcontext gc, double* ascent,
                       double* descent, double* width, pDevDesc dd) {
  /* DOCS: http://www.imagemagick.org/Magick++/TypeMetric.html */
  BEGIN_RCPP
  bool is_unicode = mbcslocale;
  if (c < 0) {
    is_unicode = true;
    c = -c;
  }

  // Convert to string - negative implies unicode code point
  char str[16];
  if (is_unicode) {
    Rf_ucstoutf8(str, (unsigned int) c);
  } else {
    str[0] = (char) c;
    str[1] = '\0';
  }

  Frame * graph = getgraph(dd);
  double multiplier = 1/dd->ipr[0]/72;
  graph->fontPointsize(gc->ps * gc->cex * multiplier);
#if MagickLibVersion >= 0x692
  graph->fontFamily(fontname(gc));
  graph->fontWeight(weight(gc->fontface));
  graph->fontStyle(style(gc->fontface));
#endif
  Magick::TypeMetric tm;
  graph->fontTypeMetrics(str, &tm);
  *ascent = tm.ascent();
  *descent = std::abs(tm.descent()); //I think this should be positive?
  *width = tm.textWidth();
  VOID_END_RCPP
}

static double image_strwidth(const char *str, const pGEcontext gc, pDevDesc dd) {
  BEGIN_RCPP
  Frame * graph = getgraph(dd);
#if MagickLibVersion >= 0x692
  graph->fontFamily(fontname(gc));
  graph->fontWeight(weight(gc->fontface));
  graph->fontStyle(style(gc->fontface));
#endif
  double multiplier = 1/dd->ipr[0]/72;
  graph->fontPointsize(gc->ps * gc->cex * multiplier);
  Magick::TypeMetric tm;
  graph->fontTypeMetrics(str, &tm);
  return tm.textWidth();
  VOID_END_RCPP
  return 0;
}

/* See r-base 'BMDeviceDriver' and 'svglite' for other examples */
static pDevDesc magick_driver_new(MagickDevice * device, int bg, int width, int height,
                                  double ps, int res, bool canclip) {

  /* from r-base BMDeviceDriver */
  int res0 = (res > 0) ? res : 72;

  /* init */
  pDevDesc dd = (DevDesc*) calloc(1, sizeof(DevDesc));
  dd->startfill = bg;
  dd->startcol = R_RGB(0, 0, 0);
  dd->startps = ps;
  dd->startlty = 0;
  dd->startfont = 1;
  dd->startgamma = 1;

  // Callbacks
  dd->activate = NULL;
  dd->deactivate = NULL;
  dd->close = image_close;
  dd->clip = image_clip;
  dd->size = image_size;
  dd->newPage = image_new_page;
  dd->line = image_line;
  dd->text = image_text;
  dd->strWidth = image_strwidth;
  dd->rect = image_rect;
  dd->circle = image_circle;
  dd->polygon = image_polygon;
  dd->polyline = image_polyline;
  dd->path = image_path;
  dd->mode = image_mode;
  dd->metricInfo = image_metric_info;
  dd->cap = image_capture;
  dd->raster = image_raster;

  // See also BMDeviceDriver
#ifdef __APPLE__
  dd->wantSymbolUTF8 = TRUE;
#else
  dd->wantSymbolUTF8 = FALSE;
#endif

  dd->hasTextUTF8 = (Rboolean) 1;
  dd->textUTF8 = image_text;
  dd->strWidthUTF8 = image_strwidth;

  // Screen Dimensions in pts
  dd->left = 0;
  dd->top = 0;
  dd->right = width;
  dd->bottom = height;

  // Magic constants copied from other graphics devices
  // nominal character sizes in pts
  dd->cra[0] = 0.9 * ps * res0/72.0;
  dd->cra[1] = 1.2 * ps * res0/72.0;

  // character alignment offsets
  dd->xCharOffset = 0.4900;
  dd->yCharOffset = 0.3333;
  dd->yLineBias = 0.2;
  // inches per pt
  dd->ipr[0] = 1.0 / res0;
  dd->ipr[1] = 1.0 / res0;

  // Capabilities
  dd->canClip = (Rboolean) canclip;
  dd->canHAdj = 0;
  dd->canChangeGamma = FALSE;
  dd->displayListOn = FALSE;
  dd->haveTransparency = 2;
  dd->haveTransparentBg = 2;
  dd->haveRaster = 2;
  dd->haveCapture = 2;
  dd->deviceSpecific = device;
  return dd;
}

/* Adapted from svglite */
static void makeDevice(MagickDevice * device, std::string bg_, int width, int height, double pointsize, int res, bool canclip) {
  int bg = R_GE_str2col(bg_.c_str());
  R_GE_checkVersionOrDie(R_GE_version);
  R_CheckDeviceAvailable();
  BEGIN_SUSPEND_INTERRUPTS {
    pDevDesc dev = magick_driver_new(device, bg, width, height, pointsize, res, canclip);
    if (dev == NULL)
      throw std::runtime_error("Failed to start Magick device");
    pGEDevDesc dd = GEcreateDevDesc(dev);
    GEaddDevice2(dd, "magick");
    GEinitDisplayList(dd);
  } END_SUSPEND_INTERRUPTS;
}

// [[Rcpp::export]]
XPtrImage magick_device_internal(std::string bg, int width, int height, double pointsize,
                                 int res, bool clip, bool antialias, bool drawing) {
  MagickDevice * device = new MagickDevice(drawing, antialias);
  device->ptr.attr("class") = Rcpp::CharacterVector::create("magick-image");
  makeDevice(device, bg, width, height, pointsize, res, clip);
  return device->ptr;
}


// [[Rcpp::export]]
XPtrImage magick_device_get(int n){
  if(n <= 1)
    throw std::runtime_error("No such graphics device");
  pGEDevDesc gd = GEgetDevice(n - 1);
  if(!gd)
    throw std::runtime_error("No such graphics device");
  return getptr(gd->dev);
}

// [[Rcpp::export]]
SEXP magick_device_pop(){
  if(dirty == NULL)
    return R_NilValue;
  MagickDevice * device = dirty;
  dirty = NULL;
  return device->ptr;
}

#include "ui_controls.h"

#include <algorithm>
#include <cmath>

namespace mgp {
namespace win {
namespace ui {

// ======================================================================= Fonts
static HFONT MakeFont(int px, int weight, int dpi) {
  return CreateFontW(-S(px, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void Fonts::Create(int dpiValue) {
  Destroy();
  dpi = dpiValue;
  title = MakeFont(22, FW_SEMIBOLD, dpi);
  big = MakeFont(28, FW_SEMIBOLD, dpi);
  watt = MakeFont(54, FW_SEMIBOLD, dpi);
  wattUnit = MakeFont(20, FW_SEMIBOLD, dpi);
  normal = MakeFont(14, FW_NORMAL, dpi);
  semibold = MakeFont(14, FW_SEMIBOLD, dpi);
  small = MakeFont(12, FW_NORMAL, dpi);
  tiny = MakeFont(11, FW_NORMAL, dpi);
  tinyBold = MakeFont(10, FW_BOLD, dpi);
}

void Fonts::Destroy() {
  HFONT* all[] = {&title, &big, &watt, &wattUnit, &normal, &semibold, &small, &tiny, &tinyBold};
  for (HFONT* f : all) {
    if (*f) DeleteObject(*f);
    *f = nullptr;
  }
}

// ======================================================================= Canvas
namespace {

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline float SdRoundRect(float px, float py, float cx, float cy, float hx, float hy, float r) {
  const float qx = std::fabs(px - cx) - (hx - r);
  const float qy = std::fabs(py - cy) - (hy - r);
  const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

inline void BlendPx(unsigned int* p, COLORREF c, float a) {
  if (a <= 0.f) return;
  if (a > 1.f) a = 1.f;
  const unsigned int d = *p;
  const float dr = static_cast<float>((d >> 16) & 255), dg = static_cast<float>((d >> 8) & 255),
              db = static_cast<float>(d & 255);
  const unsigned int r = static_cast<unsigned int>(dr + (static_cast<float>(GetRValue(c)) - dr) * a + 0.5f);
  const unsigned int g = static_cast<unsigned int>(dg + (static_cast<float>(GetGValue(c)) - dg) * a + 0.5f);
  const unsigned int b = static_cast<unsigned int>(db + (static_cast<float>(GetBValue(c)) - db) * a + 0.5f);
  *p = (r << 16) | (g << 8) | b;
}

}  // namespace

Canvas::Canvas(int w, int h) : w_(w > 0 ? w : 1), h_(h > 0 ? h : 1) {
  dc_ = CreateCompatibleDC(nullptr);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w_;
  bi.bmiHeader.biHeight = -h_;  // yukarıdan aşağıya
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  bmp_ = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bmp_ && bits) {
    bits_ = static_cast<unsigned int*>(bits);
    old_ = SelectObject(dc_, bmp_);
  }
}

Canvas::~Canvas() {
  if (old_) SelectObject(dc_, old_);
  if (bmp_) DeleteObject(bmp_);
  if (dc_) DeleteDC(dc_);
}

void Canvas::Clear(COLORREF c) {
  if (!bits_) return;
  const unsigned int v = (static_cast<unsigned int>(GetRValue(c)) << 16) | (static_cast<unsigned int>(GetGValue(c)) << 8) |
                         static_cast<unsigned int>(GetBValue(c));
  std::fill(bits_, bits_ + static_cast<size_t>(w_) * static_cast<size_t>(h_), v);
}

void Canvas::FillRoundRect(float x, float y, float w, float h, float r, COLORREF c, float alpha) {
  if (!bits_ || w <= 0 || h <= 0) return;
  r = std::min(r, std::min(w, h) / 2.f);
  const float cx = x + w / 2.f, cy = y + h / 2.f, hx = w / 2.f, hy = h / 2.f;
  const int x0 = std::max(0, static_cast<int>(std::floor(x)) - 1), x1 = std::min(w_ - 1, static_cast<int>(std::ceil(x + w)) + 1);
  const int y0 = std::max(0, static_cast<int>(std::floor(y)) - 1), y1 = std::min(h_ - 1, static_cast<int>(std::ceil(y + h)) + 1);
  for (int py = y0; py <= y1; ++py)
    for (int px = x0; px <= x1; ++px) {
      const float d = SdRoundRect(static_cast<float>(px) + .5f, static_cast<float>(py) + .5f, cx, cy, hx, hy, r);
      BlendPx(&bits_[static_cast<size_t>(py) * static_cast<size_t>(w_) + static_cast<size_t>(px)], c, Clamp01(.5f - d) * alpha);
    }
}

void Canvas::StrokeRoundRect(float x, float y, float w, float h, float r, float t, COLORREF c, float alpha) {
  if (!bits_ || w <= 0 || h <= 0) return;
  r = std::min(r, std::min(w, h) / 2.f);
  const float cx = x + w / 2.f, cy = y + h / 2.f, hx = w / 2.f, hy = h / 2.f;
  const int x0 = std::max(0, static_cast<int>(std::floor(x)) - 1), x1 = std::min(w_ - 1, static_cast<int>(std::ceil(x + w)) + 1);
  const int y0 = std::max(0, static_cast<int>(std::floor(y)) - 1), y1 = std::min(h_ - 1, static_cast<int>(std::ceil(y + h)) + 1);
  for (int py = y0; py <= y1; ++py)
    for (int px = x0; px <= x1; ++px) {
      const float d = SdRoundRect(static_cast<float>(px) + .5f, static_cast<float>(py) + .5f, cx, cy, hx, hy, r);
      const float cov = Clamp01(.5f - d) * Clamp01(d + t + .5f);  // dış kenarın içinde, iç kenarın dışında
      BlendPx(&bits_[static_cast<size_t>(py) * static_cast<size_t>(w_) + static_cast<size_t>(px)], c, cov * alpha);
    }
}

void Canvas::FillCircle(float cx, float cy, float r, COLORREF c, float alpha) {
  if (!bits_ || r <= 0) return;
  const int x0 = std::max(0, static_cast<int>(std::floor(cx - r)) - 1), x1 = std::min(w_ - 1, static_cast<int>(std::ceil(cx + r)) + 1);
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - r)) - 1), y1 = std::min(h_ - 1, static_cast<int>(std::ceil(cy + r)) + 1);
  for (int py = y0; py <= y1; ++py)
    for (int px = x0; px <= x1; ++px) {
      const float dx = static_cast<float>(px) + .5f - cx, dy = static_cast<float>(py) + .5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy) - r;
      BlendPx(&bits_[static_cast<size_t>(py) * static_cast<size_t>(w_) + static_cast<size_t>(px)], c, Clamp01(.5f - d) * alpha);
    }
}

void Canvas::Line(float x1, float y1, float x2, float y2, float thickness, COLORREF c, float alpha) {
  if (!bits_) return;
  const float rad = thickness / 2.f;
  const int bx0 = std::max(0, static_cast<int>(std::floor(std::min(x1, x2) - rad)) - 1);
  const int bx1 = std::min(w_ - 1, static_cast<int>(std::ceil(std::max(x1, x2) + rad)) + 1);
  const int by0 = std::max(0, static_cast<int>(std::floor(std::min(y1, y2) - rad)) - 1);
  const int by1 = std::min(h_ - 1, static_cast<int>(std::ceil(std::max(y1, y2) + rad)) + 1);
  const float vx = x2 - x1, vy = y2 - y1;
  const float len2 = vx * vx + vy * vy;
  for (int py = by0; py <= by1; ++py)
    for (int px = bx0; px <= bx1; ++px) {
      const float sx = static_cast<float>(px) + .5f - x1, sy = static_cast<float>(py) + .5f - y1;
      float t = len2 > 0.f ? (sx * vx + sy * vy) / len2 : 0.f;
      t = Clamp01(t);
      const float dx = sx - vx * t, dy = sy - vy * t;
      const float d = std::sqrt(dx * dx + dy * dy) - rad;
      BlendPx(&bits_[static_cast<size_t>(py) * static_cast<size_t>(w_) + static_cast<size_t>(px)], c, Clamp01(.5f - d) * alpha);
    }
}

void Canvas::Text(HFONT f, const std::wstring& s, RECT rc, COLORREF c, UINT flags) {
  if (!dc_ || s.empty()) return;
  HGDIOBJ of = SelectObject(dc_, f);
  SetBkMode(dc_, TRANSPARENT);
  SetTextColor(dc_, c);
  DrawTextW(dc_, s.c_str(), static_cast<int>(s.size()), &rc, flags | DT_NOPREFIX);
  SelectObject(dc_, of);
}

SIZE Canvas::Measure(HFONT f, const std::wstring& s) {
  SIZE sz = {0, 0};
  if (!dc_) return sz;
  HGDIOBJ of = SelectObject(dc_, f);
  GetTextExtentPoint32W(dc_, s.c_str(), static_cast<int>(s.size()), &sz);
  SelectObject(dc_, of);
  return sz;
}

void Canvas::BlitTo(HDC dst, int x, int y) const { BitBlt(dst, x, y, w_, h_, dc_, 0, 0, SRCCOPY); }

// ======================================================================= Özel kontrol
namespace {

const wchar_t kClassName[] = L"MgpControl";
bool g_kbNav = false;

struct CreateInit {
  Kind kind;
  const Fonts* fonts;
  int dpi;
  COLORREF bg;
};

struct CtlState {
  Kind kind = Kind::Action;
  std::wstring sub;
  int flags = 0;
  bool checked = false, hot = false, down = false, tracking = false;
  // Anahtar (Kind::Check) animasyonu: pos 0 (kapalı) .. 1 (açık). Zamana bağlı hesaplanır (sürüklenme/birikme yok).
  float pos = 0.f, animFrom = 0.f, animTo = 0.f;
  ULONGLONG animT0 = 0;
  bool anim = false;
  const Fonts* fonts = nullptr;
  int dpi = 96;
  COLORREF bg = kBg;
};

constexpr UINT_PTR kAnimTimer = 77;
constexpr float kAnimMs = 140.f;

CtlState* St(HWND h) { return reinterpret_cast<CtlState*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

std::wstring TextOf(HWND h) {
  const int n = GetWindowTextLengthW(h);
  std::wstring s(static_cast<size_t>(n) + 1, L'\0');
  const int got = GetWindowTextW(h, &s[0], n + 1);
  s.resize(static_cast<size_t>(got > 0 ? got : 0));
  return s;
}

void Notify(HWND h) {
  HWND p = GetParent(h);
  if (p) PostMessageW(p, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), reinterpret_cast<LPARAM>(h));
}

COLORREF Mix(COLORREF a, COLORREF b, float t) {
  t = Clamp01(t);
  auto ch = [t](int ca, int cb) { return static_cast<int>(static_cast<float>(ca) + static_cast<float>(cb - ca) * t + 0.5f); };
  return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

// Anahtarın durumunu değiştirir. Pencere görünürse konum kısa süre kayarak değişir; görünür değilse (oluşturma anı) anında.
void SetChecked(HWND h, CtlState* st, bool v, bool animate) {
  if (st->checked == v) return;
  st->checked = v;
  const float target = v ? 1.f : 0.f;
  if (!animate || !IsWindowVisible(h)) {
    st->pos = target;
    st->anim = false;
    KillTimer(h, kAnimTimer);
  } else {
    st->animFrom = st->pos;
    st->animTo = target;
    st->animT0 = GetTickCount64();
    st->anim = true;
    SetTimer(h, kAnimTimer, 15, nullptr);
  }
  InvalidateRect(h, nullptr, FALSE);
}

void StepAnim(HWND h, CtlState* st) {
  if (!st->anim) {
    KillTimer(h, kAnimTimer);
    return;
  }
  const float t = static_cast<float>(GetTickCount64() - st->animT0) / kAnimMs;
  if (t >= 1.f) {
    st->pos = st->animTo;
    st->anim = false;
    KillTimer(h, kAnimTimer);
  } else {
    const float e = t * t * (3.f - 2.f * t);  // smoothstep
    st->pos = st->animFrom + (st->animTo - st->animFrom) * e;
  }
  InvalidateRect(h, nullptr, FALSE);
}

void Click(HWND h, CtlState* st) {
  if (st->kind == Kind::Check) SetChecked(h, st, !st->checked, true);
  InvalidateRect(h, nullptr, FALSE);
  Notify(h);
}

void PaintProfile(Canvas& cv, HWND h, const CtlState& st) {
  const float W = static_cast<float>(cv.w()), H = static_cast<float>(cv.h());
  const int dpi = st.dpi;
  const bool enabled = IsWindowEnabled(h) != FALSE;
  const bool active = (st.flags & kFlagActive) != 0, busy = (st.flags & kFlagBusy) != 0;
  const bool dim = !enabled || (st.flags & kFlagDim) != 0;
  const bool focused = GetFocus() == h;

  COLORREF fill = active ? kAccentDeep : (st.down ? kCard : (st.hot && !dim ? kCardHover : kCardHi));
  if (dim && !active) fill = kCard;
  cv.Clear(st.bg);
  cv.FillRoundRect(0, 0, W, H, static_cast<float>(S(12, dpi)), fill);
  if (busy) cv.StrokeRoundRect(0, 0, W, H, static_cast<float>(S(12, dpi)), 2.f * static_cast<float>(dpi) / 96.f, kAccentHi);
  else if (active) cv.StrokeRoundRect(0, 0, W, H, static_cast<float>(S(12, dpi)), 2.f * static_cast<float>(dpi) / 96.f, kAccent);
  else cv.StrokeRoundRect(0, 0, W, H, static_cast<float>(S(12, dpi)), 1.f * static_cast<float>(dpi) / 96.f,
                         (st.hot && !dim) ? kTextFaint : kBorder);

  // "AKTİF" etiketi (üst orta)
  if (active) {
    const std::wstring tag = L"AKTİF";
    const SIZE ts = cv.Measure(st.fonts->tinyBold, tag);
    const float pw = static_cast<float>(ts.cx + S(14, dpi)), ph = static_cast<float>(S(16, dpi));
    const float px = (W - pw) / 2.f, py = static_cast<float>(S(8, dpi));
    cv.FillRoundRect(px, py, pw, ph, ph / 2.f, kAccent);
    RECT tr = {static_cast<LONG>(px), static_cast<LONG>(py), static_cast<LONG>(px + pw), static_cast<LONG>(py + ph)};
    cv.Text(st.fonts->tinyBold, tag, tr, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // "140 W": sayı büyük, birim küçük
  const std::wstring full = TextOf(h);
  const size_t sp = full.find(L' ');
  const std::wstring num = sp == std::wstring::npos ? full : full.substr(0, sp);
  const std::wstring unit = sp == std::wstring::npos ? std::wstring() : full.substr(sp + 1);
  const SIZE sn = cv.Measure(st.fonts->big, num), su = cv.Measure(st.fonts->small, unit);
  const int gap = unit.empty() ? 0 : S(4, dpi);
  const int total = sn.cx + gap + su.cx;
  const int x0 = (cv.w() - total) / 2, ny = S(25, dpi);
  const COLORREF numCol = dim && !active ? kTextFaint : kText;
  RECT rn = {x0, ny, x0 + sn.cx + 2, ny + sn.cy};
  cv.Text(st.fonts->big, num, rn, numCol, DT_LEFT | DT_TOP | DT_SINGLELINE);
  RECT ru = {x0 + sn.cx + gap, ny, x0 + sn.cx + gap + su.cx + 2, ny + sn.cy - S(5, dpi)};
  cv.Text(st.fonts->small, unit, ru, dim && !active ? kTextFaint : kTextDim, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);

  // alt başlık
  RECT rs = {S(6, dpi), cv.h() - S(27, dpi), cv.w() - S(6, dpi), cv.h() - S(9, dpi)};
  if (busy) cv.Text(st.fonts->tiny, L"Uygulanıyor…", rs, kAccentHi, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  else cv.Text(st.fonts->tiny, st.sub, rs, active ? kAccentHi : (dim ? kTextFaint : kTextDim),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  if (focused && g_kbNav)
    cv.StrokeRoundRect(static_cast<float>(S(3, dpi)), static_cast<float>(S(3, dpi)), W - static_cast<float>(S(6, dpi)),
                       H - static_cast<float>(S(6, dpi)), static_cast<float>(S(10, dpi)), 2.f * static_cast<float>(dpi) / 96.f, kText, .85f);
}

void PaintCheck(Canvas& cv, HWND h, const CtlState& st) {
  // Tema renkli açma/kapama anahtarı: ray + kayan yuvarlak. Açık = vurgu rengi (kırmızı), kapalı = koyu iç + ince çerçeve.
  // kFlagDim: şu an kullanılamaz (soluk çizilir ama tıklanabilir/odaklanabilir kalır; tıklama nedenini açıklar).
  const float W = static_cast<float>(cv.w()), H = static_cast<float>(cv.h());
  const int dpi = st.dpi;
  const bool enabled = IsWindowEnabled(h) != FALSE && (st.flags & kFlagDim) == 0;
  const float line = static_cast<float>(dpi) / 96.f;
  const float trackW = static_cast<float>(S(40, dpi)), trackH = static_cast<float>(S(22, dpi));
  const float tx = static_cast<float>(S(2, dpi)), ty = (H - trackH) / 2.f;
  const float p = Clamp01(st.pos);
  cv.Clear(st.bg);

  const COLORREF offFill = (st.bg == kBg) ? kCard : kBg;
  const COLORREF onFill = enabled ? (st.hot ? kAccentHi : kAccent) : kAccentDeep;
  cv.FillRoundRect(tx, ty, trackW, trackH, trackH / 2.f, Mix(offFill, onFill, p));
  const COLORREF offLine = enabled ? (st.hot ? kTextDim : kTextFaint) : kBorder;
  if (p < 0.999f) cv.StrokeRoundRect(tx, ty, trackW, trackH, trackH / 2.f, 1.5f * line, offLine, 1.f - p);
  if (!enabled && p > 0.001f) cv.StrokeRoundRect(tx, ty, trackW, trackH, trackH / 2.f, line, kBorder, p);

  const float margin = static_cast<float>(S(4, dpi));
  const float r0 = (trackH - 2.f * margin) / 2.f;
  const float cxOff = tx + margin + r0, cxOn = tx + trackW - margin - r0, cy = ty + trackH / 2.f;
  const float r = r0 + (st.down && enabled ? 1.2f * line : 0.f);
  const COLORREF offThumb = enabled ? (st.hot ? kText : kTextDim) : kTextFaint;
  const COLORREF onThumb = enabled ? RGB(255, 255, 255) : kTextFaint;
  cv.FillCircle(cxOff + (cxOn - cxOff) * p, cy, r, Mix(offThumb, onThumb, p));

  RECT rl = {static_cast<LONG>(tx + trackW) + S(10, dpi), 0, cv.w(), cv.h()};
  cv.Text(st.fonts->normal, TextOf(h), rl, enabled ? kText : kTextFaint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (GetFocus() == h && g_kbNav)
    cv.StrokeRoundRect(0, 0, W, H, static_cast<float>(S(7, dpi)), 1.5f * line, kText, .6f);
}

void PaintAction(Canvas& cv, HWND h, const CtlState& st) {
  const float W = static_cast<float>(cv.w()), H = static_cast<float>(cv.h());
  const int dpi = st.dpi;
  const bool enabled = IsWindowEnabled(h) != FALSE;
  const float line = static_cast<float>(dpi) / 96.f;
  cv.Clear(st.bg);
  const COLORREF fill = !enabled ? kCard : (st.down ? kCard : (st.hot ? kCardHover : kCardHi));
  cv.FillRoundRect(0, 0, W, H, static_cast<float>(S(9, dpi)), fill);
  cv.StrokeRoundRect(0, 0, W, H, static_cast<float>(S(9, dpi)), line, (st.hot && enabled) ? kTextFaint : kBorder);
  RECT rc = {S(8, dpi), 0, cv.w() - S(8, dpi), cv.h()};
  cv.Text(st.fonts->normal, TextOf(h), rc, enabled ? kText : kTextFaint, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (GetFocus() == h && g_kbNav)
    cv.StrokeRoundRect(static_cast<float>(S(3, dpi)), static_cast<float>(S(3, dpi)), W - static_cast<float>(S(6, dpi)),
                       H - static_cast<float>(S(6, dpi)), static_cast<float>(S(7, dpi)), 1.5f * line, kText, .85f);
}

LRESULT CALLBACK CtlProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  CtlState* st = St(h);
  switch (m) {
    case WM_NCCREATE: {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
      const CreateInit* ci = static_cast<const CreateInit*>(cs->lpCreateParams);
      CtlState* s = new CtlState();
      if (ci) {
        s->kind = ci->kind;
        s->fonts = ci->fonts;
        s->dpi = ci->dpi;
        s->bg = ci->bg;
      }
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
      return DefWindowProcW(h, m, wp, lp);
    }
    case WM_NCDESTROY:
      delete st;
      SetWindowLongPtrW(h, GWLP_USERDATA, 0);
      return DefWindowProcW(h, m, wp, lp);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT rc;
      GetClientRect(h, &rc);
      if (st && st->fonts && rc.right > 0 && rc.bottom > 0) {
        Canvas cv(rc.right, rc.bottom);
        if (cv.ok()) {
          switch (st->kind) {
            case Kind::Profile: PaintProfile(cv, h, *st); break;
            case Kind::Check: PaintCheck(cv, h, *st); break;
            case Kind::Action: PaintAction(cv, h, *st); break;
          }
          cv.BlitTo(dc, 0, 0);
        }
      }
      EndPaint(h, &ps);
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT && IsWindowEnabled(h)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;
    case WM_MOUSEMOVE:
      if (st && !st->hot) {
        st->hot = true;
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case WM_MOUSELEAVE:
      if (st) {
        st->hot = false;
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (st) {
        g_kbNav = false;
        SetFocus(h);
        SetCapture(h);
        st->down = true;
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (st && st->down) {
        st->down = false;
        const bool had = GetCapture() == h;
        if (had) ReleaseCapture();
        RECT rc;
        GetClientRect(h, &rc);
        const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
        if (had && PtInRect(&rc, pt)) Click(h, st);
        else InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case WM_CAPTURECHANGED:
      if (st && st->down) {
        st->down = false;
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      if (st && m == WM_KILLFOCUS) st->down = false;
      InvalidateRect(h, nullptr, FALSE);
      return 0;
    case WM_KEYDOWN:
      if (st) {
        if (wp == VK_SPACE) {
          g_kbNav = true;
          st->down = true;
          InvalidateRect(h, nullptr, FALSE);
          return 0;
        }
        if (wp == VK_RETURN) {
          g_kbNav = true;
          Click(h, st);
          return 0;
        }
      }
      break;
    case WM_KEYUP:
      if (st && wp == VK_SPACE && st->down) {
        st->down = false;
        Click(h, st);
        return 0;
      }
      break;
    case WM_TIMER:
      if (st && wp == kAnimTimer) {
        StepAnim(h, st);
        return 0;
      }
      break;
    case WM_ENABLE:
    case WM_SETTEXT: {
      const LRESULT r = DefWindowProcW(h, m, wp, lp);
      InvalidateRect(h, nullptr, FALSE);
      return r;
    }
    case MGPM_SETSUB:
      if (st) {
        st->sub = lp ? reinterpret_cast<const wchar_t*>(lp) : L"";
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case MGPM_SETFLAGS:
      if (st && st->flags != static_cast<int>(wp)) {
        st->flags = static_cast<int>(wp);
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case MGPM_SETCHECK:
      if (st) {
        if (st->kind == Kind::Check) SetChecked(h, st, wp != 0, true);
        else {
          st->checked = wp != 0;
          InvalidateRect(h, nullptr, FALSE);
        }
      }
      return 0;
    case MGPM_GETCHECK:
      return st && st->checked ? 1 : 0;
    case MGPM_SETSTYLE:
      if (st) {
        st->dpi = static_cast<int>(wp);
        st->fonts = reinterpret_cast<const Fonts*>(lp);
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

}  // namespace

void SetKeyboardNav(bool on) { g_kbNav = on; }

bool RegisterControlClass(HINSTANCE hi) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = CtlProc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND CreateControl(HWND parent, int id, Kind kind, const wchar_t* text, const Fonts* fonts, int dpi, COLORREF bg) {
  CreateInit ci = {kind, fonts, dpi, bg};
  return CreateWindowExW(0, kClassName, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), &ci);
}

}  // namespace ui
}  // namespace win
}  // namespace mgp

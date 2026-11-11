// Hafif, bağımlılıksız (yalnız GDI/User32) çizim ve özel kontroller.
// Şekiller yazılımsal işaretli-mesafe-alanı (SDF) ile kenar yumuşatmalı çizilir; metin GDI ile (ClearType).
#pragma once
#include <string>

#include "win_util.h"

namespace mgp {
namespace win {
namespace ui {

// ---- Palet: antrasit + koyu kırmızı vurgu (Monster yeşili YOK) -------------------------------------------
inline constexpr COLORREF kBg = RGB(0x15, 0x17, 0x1B);
inline constexpr COLORREF kCard = RGB(0x1D, 0x20, 0x26);
inline constexpr COLORREF kCardHi = RGB(0x25, 0x29, 0x31);
inline constexpr COLORREF kCardHover = RGB(0x2F, 0x34, 0x3D);
inline constexpr COLORREF kBorder = RGB(0x36, 0x3B, 0x45);
inline constexpr COLORREF kText = RGB(0xE8, 0xEA, 0xED);
inline constexpr COLORREF kTextDim = RGB(0x9D, 0xA4, 0xAE);
inline constexpr COLORREF kTextFaint = RGB(0x6B, 0x72, 0x7C);
inline constexpr COLORREF kAccent = RGB(0xB8, 0x40, 0x4B);
inline constexpr COLORREF kAccentHi = RGB(0xD2, 0x57, 0x63);
inline constexpr COLORREF kAccentDeep = RGB(0x3B, 0x1F, 0x25);
inline constexpr COLORREF kInfo = RGB(0x8F, 0xB4, 0xCC);   // çelik mavisi
inline constexpr COLORREF kWarn = RGB(0xE0, 0xA8, 0x3C);
inline constexpr COLORREF kErr = RGB(0xE8, 0x5B, 0x68);

inline int S(int v, int dpi) { return MulDiv(v, dpi, 96); }

// Açma/kapama anahtarı (Kind::Check) geometrisi (mantıksal px): sol boşluk 2 + ray 40 + etiket boşluğu 10. Açıklama
// metinleri etiketle hizalansın diye ana pencere bu değeri kullanır.
inline constexpr int kSwitchTextIndent = 52;

struct Fonts {
  HFONT title = nullptr, big = nullptr, watt = nullptr, wattUnit = nullptr, normal = nullptr, semibold = nullptr,
        small = nullptr, tiny = nullptr, tinyBold = nullptr;
  int dpi = 96;
  void Create(int dpiValue);
  void Destroy();
};

// ---- Çizim yüzeyi ---------------------------------------------------------------------------------------
class Canvas {
 public:
  Canvas(int w, int h);
  ~Canvas();
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;
  bool ok() const { return bits_ != nullptr; }
  HDC dc() const { return dc_; }
  int w() const { return w_; }
  int h() const { return h_; }

  void Clear(COLORREF c);
  void FillRoundRect(float x, float y, float w, float h, float r, COLORREF c, float alpha = 1.f);
  // Kenar çizgisi şeklin İÇİNDE kalır (kalınlık t).
  void StrokeRoundRect(float x, float y, float w, float h, float r, float t, COLORREF c, float alpha = 1.f);
  void FillCircle(float cx, float cy, float r, COLORREF c, float alpha = 1.f);
  void Line(float x1, float y1, float x2, float y2, float thickness, COLORREF c, float alpha = 1.f);
  // Metin: font seçilir, saydam zemin, verilen dikdörtgene hizalanır (DrawText bayrakları).
  void Text(HFONT f, const std::wstring& s, RECT rc, COLORREF c, UINT flags);
  SIZE Measure(HFONT f, const std::wstring& s);
  void BlitTo(HDC dst, int x, int y) const;

 private:
  HDC dc_ = nullptr;
  HBITMAP bmp_ = nullptr;
  HGDIOBJ old_ = nullptr;
  unsigned int* bits_ = nullptr;
  int w_ = 0, h_ = 0;
};

// ---- Özel kontrol sınıfı ---------------------------------------------------------------------------------
// Check: tema renkli açma/kapama anahtarı (switch). Davranış (tıklama/Boşluk/Enter ile değişir, MGPM_SETCHECK/GETCHECK) onay
// kutusuyla aynıdır; yalnız görünüm değişti.
// Mini: daha küçük eylem düğmesi ("Hakkında"). Lang: iki bölmeli "EN | TR" dil seçici (MGPM_SETCHECK/GETCHECK: 1 = EN, 0 = TR).
enum class Kind { Profile, Check, Action, Mini, Lang };

enum : UINT {
  MGPM_SETSUB = WM_APP + 100,   // lParam: const wchar_t* alt başlık (Profile)
  MGPM_SETFLAGS,                // wParam: bayraklar
  MGPM_SETCHECK,                // wParam: 0/1 (Check)
  MGPM_GETCHECK,                // sonuç: 0/1
  MGPM_SETSTYLE                 // wParam: dpi, lParam: const Fonts*
};

enum : int {
  kFlagActive = 1,  // EC+NVIDIA'nın doğruladığı aktif profil
  kFlagBusy = 2,    // şu an uygulanıyor
  kFlagDim = 4      // şu an kullanılamaz (soluk çizilir ama odaklanabilir; tıklama nedenini açıklar)
};

bool RegisterControlClass(HINSTANCE hi);
HWND CreateControl(HWND parent, int id, Kind kind, const wchar_t* text, const Fonts* fonts, int dpi, COLORREF bg);

// Klavye ile gezinme yapıldığında odak halkası gösterilir; fare ile tıklayınca gizlenir.
void SetKeyboardNav(bool on);

}  // namespace ui
}  // namespace win
}  // namespace mgp

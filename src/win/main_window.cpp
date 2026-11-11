#include "main_window.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <memory>

#include "../../res/resource.h"
#include "file_logger.h"
#include "main_window_ids.h"
#include "settings_store.h"
#ifdef MGP_DEMO
#include "demo_backends.h"
#endif

namespace mgp {
namespace win {

using txt::Level;
using txt::Notice;

namespace {

using namespace ids;

enum : int { IDM_SHOW = 2001, IDM_INFO, IDM_EXIT };

// Tepsi simgesi eklenemezse (Explorer/görev çubuğu oturum açılışında geç hazırlanabilir) 3 sn aralıkla en çok 20 kez (~60 sn) yeniden denenir;
// sonra uygulama ulaşılamaz kalmasın diye pencere gösterilir. Sonsuz döngü yok.
constexpr int kTrayRetryMax = 20;
constexpr UINT kTrayRetryMs = 3000;

int DpiForWindow(HWND h) {
  typedef UINT(WINAPI * Fn)(HWND);
  static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
  if (fn) {
    const UINT d = fn(h);
    if (d) return static_cast<int>(d);
  }
  HDC dc = GetDC(h);
  const int d = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(h, dc);
  return d > 0 ? d : 96;
}

std::wstring Hex2W(unsigned v) {
  wchar_t b[8];
  std::swprintf(b, 8, L"0x%02X", v & 0xFFu);
  return b;
}

int CalcTextHeight(ui::Canvas& cv, HFONT f, const std::wstring& s, int width) {
  RECT r = {0, 0, width, 0};
  HGDIOBJ of = SelectObject(cv.dc(), f);
  DrawTextW(cv.dc(), s.c_str(), static_cast<int>(s.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(cv.dc(), of);
  return r.bottom - r.top;
}

}  // namespace

const wchar_t* MainWindow::ClassName() { return L"MonsterGpuPowerMainWindow"; }
UINT MainWindow::ShowMeMessage() {
  static const UINT m = RegisterWindowMessageW(L"MonsterGpuPower.ShowMe");
  return m;
}

MainWindow::MainWindow(HINSTANCE hi, Backends& be, ILog& log, const Settings& settings)
    : hi_(hi), be_(be), log_(log), settings_(settings) {}

MainWindow::~MainWindow() {
  if (!worker_.Stop(20000)) {
    log_.Write(LogLevel::Error, "worker did not stop in time during teardown; terminating process");
    ExitProcess(3);
  }
  fonts_.Destroy();
  if (iconBig_) DestroyIcon(iconBig_);
  if (iconSmall_) DestroyIcon(iconSmall_);
}

// ======================================================================= oluşturma
LRESULT CALLBACK MainWindow::WndProcStatic(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  MainWindow* self = nullptr;
  if (m == WM_NCCREATE) {
    self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    self->hwnd_ = h;
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(h, m, wp, lp);
  return self->WndProc(m, wp, lp);
}

void MainWindow::LoadIcons() {
  iconBig_ = static_cast<HICON>(LoadImageW(hi_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                           GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
  iconSmall_ = static_cast<HICON>(LoadImageW(hi_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
}

void MainWindow::ApplyDwm() {
  HMODULE d = LoadLibraryW(L"dwmapi.dll");
  if (!d) return;
  typedef HRESULT(WINAPI * Fn)(HWND, DWORD, LPCVOID, DWORD);
  Fn f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(d, "DwmSetWindowAttribute")));
  if (f) {
    const BOOL dark = TRUE;
    f(hwnd_, 20, &dark, sizeof dark);  // DWMWA_USE_IMMERSIVE_DARK_MODE
    f(hwnd_, 19, &dark, sizeof dark);  // eski yapılar
    const DWORD round = 2;             // DWMWCP_ROUND (Windows 11)
    f(hwnd_, 33, &round, sizeof round);
    const COLORREF border = ui::kBorder, cap = ui::kBg, txt = ui::kText;
    f(hwnd_, 34, &border, sizeof border);  // DWMWA_BORDER_COLOR
    f(hwnd_, 35, &cap, sizeof cap);        // DWMWA_CAPTION_COLOR
    f(hwnd_, 36, &txt, sizeof txt);        // DWMWA_TEXT_COLOR
  }
  FreeLibrary(d);
}

bool MainWindow::Create(bool startHidden) {
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  LoadIcons();

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = &MainWindow::WndProcStatic;
  wc.hInstance = hi_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;  // WM_ERASEBKGND işlenir (çift tamponlu çizim); sınıf fırçası hiç kullanılmaz
  wc.hIcon = iconBig_;
  wc.hIconSm = iconSmall_;
  wc.lpszClassName = ClassName();
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  if (!ui::RegisterControlClass(hi_)) return false;

  const wchar_t* title = Backends::IsDemoBuild() ? txt::kWindowTitleDemo() : txt::kAppTitle;
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
  hwnd_ = CreateWindowExW(0, ClassName(), title, style, CW_USEDEFAULT, CW_USEDEFAULT, 700, 600, nullptr, nullptr, hi_, this);
  if (!hwnd_) return false;
  if (iconBig_) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconBig_));
  if (iconSmall_) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall_));
  AllowShellMessages();

  sysDpi_ = DpiForWindow(hwnd_);
  dpi_ = sysDpi_;
  fonts_.Create(dpi_);
  CreateChildren();
  Layout();
  ResizeForDpi(true);  // yerleşim ekrana sığmıyorsa ölçeği küçültür (bkz. ResizeForDpi)
  ApplyDwm();

  UpdateControls();
  RequestTaskProbe();  // "Uygulamayı açılışta başlat" durumu (arka planda; pencere hemen açılır)
  if (startHidden) {
    // Başlatan program (Görev Zamanlayıcı dahil) STARTUPINFO.wShowWindow verirse, penceredeki İLK ShowWindow çağrısının nCmdShow değeri yok sayılıp
    // o değer kullanılır (Microsoft belgesi); bu, gizli başlatmayı görünür pencereye çevirebilir. İlk çağrıyı bu yüzden "harcar", gizlemeyi ikinci
    // çağrıyla (HideToTray) kesinleştiririz; en kötü durumda çok kısa bir görünme olur. Bu davranış gerçek Windows'ta DOĞRULANMADI (cihaz testi: 9. bölüm).
    ShowWindow(hwnd_, SW_HIDE);
    HideToTray();
  } else {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
  }
  RequestPoll(true);
  RearmPoll();
  return true;
}

void MainWindow::CreateChildren() {
  for (int i = 0; i < 6; ++i) {
    const std::wstring t = std::to_wstring(kProfiles[i].watts) + L" W";
    prof_[i] = ui::CreateControl(hwnd_, IDC_PROF0 + i, ui::Kind::Profile, t.c_str(), &fonts_, dpi_, ui::kBg);
    SendMessageW(prof_[i], ui::MGPM_SETSUB, 0, reinterpret_cast<LPARAM>(txt::ProfileSub(i)));
  }
  chkAutostart_ = ui::CreateControl(hwnd_, IDC_CHK_AUTOSTART, ui::Kind::Check, txt::kAutostartLabel(), &fonts_, dpi_, ui::kBg);
  chkRestore_ = ui::CreateControl(hwnd_, IDC_CHK_RESTORE, ui::Kind::Check, txt::kRestoreLabel(), &fonts_, dpi_, ui::kBg);
  btnRefresh_ = ui::CreateControl(hwnd_, IDC_BTN_REFRESH, ui::Kind::Action, txt::kRefresh(), &fonts_, dpi_, ui::kBg);
  btnConsent_ = ui::CreateControl(hwnd_, IDC_BTN_CONSENT, ui::Kind::Action, txt::kResetConsents(), &fonts_, dpi_, ui::kBg);
  btnAbout_ = ui::CreateControl(hwnd_, IDC_BTN_ABOUT, ui::Kind::Mini, txt::kAboutLabel(), &fonts_, dpi_, ui::kBg);
  btnLang_ = ui::CreateControl(hwnd_, IDC_LANG, ui::Kind::Lang, txt::kLangToggleName(), &fonts_, dpi_, ui::kBg);
  SendMessageW(btnLang_, ui::MGPM_SETCHECK, CurrentLang() == Lang::En ? 1 : 0, 0);
}

void MainWindow::Layout() {
  const int mx = S(24), cw = S(652), gap = S(10);
  rcCard_ = {mx, S(76), mx + cw, S(76) + S(150)};
  rcNotice_ = {mx, rcCard_.bottom + S(10), mx + cw, rcCard_.bottom + S(10) + S(52)};
  const int py = rcNotice_.bottom + S(12);
  const int pw = (cw - 5 * gap) / 6, ph = S(92);
  for (int i = 0; i < 6; ++i) MoveWindow(prof_[i], mx + i * (pw + gap), py, pw, ph, TRUE);

  // ---- "KALICI UYGULAMA" bilgi kartı: profil düğmelerinin hemen altında, tam genişlikte. Etkileşimli denetim YOKTUR (her profil seçimi
  // zaten kalıcıdır); başlık, HER ZAMAN görünen açıklama, ayırıcı ve GERÇEK OEM dosyası + GERÇEK EC/NVIDIA'dan türeyen durum satırı.
  {
    const int pad = S(16);
    const int top = py + ph + S(12);
    rcPersistTitle_ = {mx + pad, top + S(12), mx + cw - pad, top + S(12) + S(16)};
    int descH = S(32);  // açıklama 1-3 satır olabilir (yazı tipi/ölçeğe göre): ölçülür, kırpılmaz
    {
      ui::Canvas tmp(1, 1);
      if (tmp.ok()) {
        const int hh = CalcTextHeight(tmp, fonts_.tiny, txt::kPersistDesc(), cw - 2 * pad);
        descH = std::min(S(52), std::max(S(16), hh + S(2)));
      }
    }
    rcPersistDesc_ = {mx + pad, top + S(32), mx + cw - pad, top + S(32) + descH};
    persistSepY_ = rcPersistDesc_.bottom + S(8);
    rcPersistStatus_ = {mx + pad, persistSepY_ + S(7), mx + cw - pad, persistSepY_ + S(7) + S(34)};
    rcPersist_ = {mx, top, mx + cw, rcPersistStatus_.bottom + S(8)};
  }

  const int oy = rcPersist_.bottom + S(14), colW = S(314), col2 = mx + colW + S(24);
  const int ch = S(26);
  const int dx = S(ui::kSwitchTextIndent);  // açıklama metni anahtarın etiketiyle hizalı başlar
  MoveWindow(chkAutostart_, mx, oy, colW, ch, TRUE);
  MoveWindow(chkRestore_, col2, oy, colW, ch, TRUE);
  rcDesc_[0] = {mx + dx, oy + S(28), mx + colW, oy + S(28) + S(30)};
  rcDesc_[1] = {col2 + dx, oy + S(28), col2 + colW, oy + S(28) + S(30)};

  const int ay = oy + S(58) + S(6), ah = S(34), aw = (cw - 3 * gap) / 4, aboutW = S(66), aboutH = S(26);
  MoveWindow(btnRefresh_, mx, ay, aw, ah, TRUE);
  MoveWindow(btnConsent_, mx + aw + gap, ay, aw, ah, TRUE);
  MoveWindow(btnAbout_, mx + cw - aboutW, ay + (ah - aboutH) / 2, aboutW, aboutH, TRUE);  // küçük düğme, satırda dikey ortalı
  // Dil seçici (EN | TR): başlık satırının sağ ucu; durum çipi bunun soluna yaslanır (bkz. PaintAll).
  rcLang_ = {mx + cw - S(58), S(24), mx + cw, S(24) + S(26)};
  MoveWindow(btnLang_, rcLang_.left, rcLang_.top, rcLang_.right - rcLang_.left, rcLang_.bottom - rcLang_.top, TRUE);
  rcFooter_ = {mx, ay + ah + S(12), mx + cw, ay + ah + S(12) + S(18)};
  clientH_ = rcFooter_.bottom + S(14);
}

void MainWindow::ApplyLayoutDpi(int d) {
  if (d == dpi_ && fonts_.dpi == d && fonts_.normal) return;
  dpi_ = d;
  fonts_.Create(dpi_);
  HWND all[] = {prof_[0], prof_[1], prof_[2], prof_[3], prof_[4], prof_[5], chkAutostart_, chkRestore_, btnRefresh_, btnConsent_, btnAbout_, btnLang_};
  for (HWND c : all)
    if (c) SendMessageW(c, ui::MGPM_SETSTYLE, static_cast<WPARAM>(dpi_), reinterpret_cast<LPARAM>(&fonts_));
}

// Pencere boyutu = yerleşim ölçeğindeki istemci alanı + monitörün GERÇEK DPI'ındaki çerçeve/başlık payı.
// Yerleşim monitörün çalışma alanına sığmıyorsa (ör. 1080p ekranda %150-%175 ölçek), TÜM arayüz
// birlikte yalnız gerektiği kadar küçültülür (yazı tipleri dahil): hiçbir bölüm kesilmez, kaydırma çubuğu gerekmez.
// Küçültme, ilk sığan yerleşim ölçeğinde durur; mutlak alt sınır 72 DPI'dır (%75). Bunun da altına inmek gerekecek kadar küçük
// bir çalışma alanı (<~600 mantıksal piksel) bu uygulamanın kapsamı dışındadır: pencere yine üst kenara hizalı kalır.
void MainWindow::ResizeForDpi(bool center) {
  typedef BOOL(WINAPI * AdjFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
  static AdjFn adj = reinterpret_cast<AdjFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi")));
  const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
  auto nonClient = [&](RECT& r) {
    if (adj) adj(&r, style, FALSE, 0, static_cast<UINT>(sysDpi_));
    else AdjustWindowRectEx(&r, style, FALSE, 0);
  };

  MONITORINFO mi = {};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
  RECT nc = {0, 0, 0, 0};
  nonClient(nc);
  const int margin = MulDiv(8, sysDpi_, 96);
  const int availH = (mi.rcWork.bottom - mi.rcWork.top) - (nc.bottom - nc.top) - margin;
  const int availW = (mi.rcWork.right - mi.rcWork.left) - (nc.right - nc.left) - margin;

  int d = sysDpi_;
  const int floorDpi = 72;  // mutlak alt sınır; yerleşim bunun üstünde İLK sığdığı ölçekte durur
  for (int i = 0; i < 24; ++i) {  // her tur d'yi en az 1 azaltır; oransal tahmin genelde 2-4 turda sığdırır
    ApplyLayoutDpi(d);
    Layout();
    const int needW = S(700);
    if ((clientH_ <= availH && needW <= availW) || d <= floorDpi) break;
    const long long byH = static_cast<long long>(d) * availH / std::max(clientH_, 1);
    const long long byW = static_cast<long long>(d) * availW / std::max(needW, 1);
    int nd = static_cast<int>(std::min(byH, byW)) - 1;
    if (nd >= d) nd = d - 1;
    d = std::max(floorDpi, nd);
  }

  RECT rc = {0, 0, S(700), clientH_};
  nonClient(rc);
  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  int x = 0, y = 0;
  UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
  if (center) {
    x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
  } else {
    flags |= SWP_NOMOVE;
  }
  SetWindowPos(hwnd_, nullptr, x, y, w, h, flags);
}

// ======================================================================= pencere prosedürü
LRESULT MainWindow::WndProc(UINT m, WPARAM wp, LPARAM lp) {
  if (m == ShowMeMessage()) {
    ShowMe();
    return 0;
  }
  if (taskbarCreated_ && m == taskbarCreated_) {
    // Explorer (yeniden) başladı ya da görev çubuğu oturum açılışında geç hazır oldu: pencere gizliyse (tepside çalışıyor) simgeyi yeniden ekle.
    if (trayShown_ || (!IsShown() && !exiting_)) {
      trayShown_ = false;
      trayRetries_ = 0;
      AddTray();
    }
    return 0;
  }
  switch (m) {
    case WM_PAINT:
      OnPaint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_COMMAND:
      if (HIWORD(wp) == BN_CLICKED) OnCommand(LOWORD(wp));
      return 0;
    case WM_TIMER:
      if (wp == IDT_POLL) RequestPoll(false);
      else if (wp == IDT_RESUME) {
        KillTimer(hwnd_, IDT_RESUME);
        RequestPoll(true);
      } else if (wp == IDT_TRAYRETRY) {
        OnTrayRetry();
      } else if (wp == IDT_EXITGUARD) {
        log_.Write(LogLevel::Warn, "exit guard timer fired; closing with a short bounded wait");
        DoExit(3000);
      }
      return 0;
    case WM_STATE:
      OnState(reinterpret_cast<LiveState*>(lp));
      return 0;
    case WM_APPLYDONE:
      OnApplyDone(reinterpret_cast<ApplyDone*>(lp));
      return 0;
    case WM_OEMSTATE:
      OnOemState(reinterpret_cast<OemMsg*>(lp));
      return 0;
    case WM_TASKDONE:
      OnTaskDone(reinterpret_cast<TaskMsg*>(lp));
      return 0;
    case WM_TRAY:
      if (lp == WM_LBUTTONDBLCLK) RestoreFromTray();
      else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowTrayMenu();
      return 0;
    case WM_SYSCOMMAND:
      if ((wp & 0xFFF0) == SC_MINIMIZE) {
        HideToTray();
        return 0;
      }
      break;
    case WM_SIZE:
      if (wp == SIZE_MINIMIZED) HideToTray();
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
        // Uykudan dönüşte EC durumu firmware tarafından değişmiş olabilir: gösterilen değerler için salt okunur tam okuma (hiçbir şey yazılmaz).
        pollCount_ = 0;
        SetTimer(hwnd_, IDT_RESUME, 3000, nullptr);
      }
      return TRUE;
    case WM_SETFOCUS:
      if (prof_[0]) SetFocus(prof_[0]);
      return 0;
    case WM_DPICHANGED: {
      sysDpi_ = HIWORD(wp);
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      ResizeForDpi(false);  // yeni monitörün DPI'ına göre yazı tipleri + yerleşim + (gerekirse) ekrana sığdırma
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_QUERYENDSESSION:
      return TRUE;  // oturum kapanmasını engellemeyiz; sürüyorsa ShutdownBlockReason + WM_ENDSESSION'da bekleme var
    case WM_ENDSESSION:
      if (wp) {
        // Sürmekte olan bir EC yazımı/geri almasını yarıda kesmeyiz (en çok 20 sn); sonra ayarlar kaydedilir.
        exiting_ = true;
        log_.Write(LogLevel::Info, "session ending; waiting for any in-flight work");
        worker_.Stop(20000);
        SaveSettings(settings_);
      }
      return 0;
    case WM_CLOSE:
      RequestExit();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, m, wp, lp);
}

bool MainWindow::PreTranslate(MSG* m) {
  if (m->message != WM_KEYDOWN || !hwnd_) return false;
  if (m->hwnd != hwnd_ && !IsChild(hwnd_, m->hwnd)) return false;
  switch (m->wParam) {
    case VK_TAB: {
      ui::SetKeyboardNav(true);
      const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      HWND cur = GetFocus();
      HWND next = GetNextDlgTabItem(hwnd_, IsChild(hwnd_, cur) ? cur : nullptr, shift ? TRUE : FALSE);
      if (next) SetFocus(next);
      return true;
    }
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN: {
      const int idx = IndexOfProfile(GetFocus());
      if (idx < 0) return false;
      const int dir = (m->wParam == VK_LEFT || m->wParam == VK_UP) ? -1 : 1;
      int n = idx + dir;
      if (n < 0) n = 0;
      if (n > 5) n = 5;
      ui::SetKeyboardNav(true);
      SetFocus(prof_[n]);
      InvalidateRect(prof_[idx], nullptr, FALSE);
      return true;
    }
    case VK_F5:
      RequestPoll(true);
      return true;
  }
  return false;
}

int MainWindow::IndexOfProfile(HWND h) const {
  for (int i = 0; i < 6; ++i)
    if (prof_[i] == h) return i;
  return -1;
}

bool MainWindow::IsShown() const { return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_); }

// ======================================================================= çizim
void MainWindow::OnPaint() {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd_, &ps);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  if (rc.right > 0 && rc.bottom > 0) {
    ui::Canvas cv(rc.right, rc.bottom);
    if (cv.ok()) {
      PaintAll(cv);
      cv.BlitTo(dc, 0, 0);
    }
  }
  EndPaint(hwnd_, &ps);
}

Notice MainWindow::CurrentNotice() const {
  if (!resultNotice_.empty()) return resultNotice_;
  Notice n;
  if (!haveState_) {
    n.text = txt::kNoticeReading();
    return n;
  }
  if (!cur_.blockers.empty()) return txt::FirstBlockerNotice(cur_);
  if (!cur_.consistent) return txt::InconsistentNotice(cur_);
  n.text = Backends::IsDemoBuild() ? txt::kNoticeDemo() : txt::kNoticeReady();
  return n;
}

void MainWindow::PaintNotice(ui::Canvas& cv) {
  const Notice n = CurrentNotice();
  COLORREF col = ui::kInfo, tint = RGB(0x1B, 0x23, 0x2B);
  if (n.level == Level::Warn) {
    col = ui::kWarn;
    tint = RGB(0x2B, 0x24, 0x16);
  } else if (n.level == Level::Error) {
    col = ui::kErr;
    tint = RGB(0x2F, 0x1A, 0x1E);
  }
  const float x = static_cast<float>(rcNotice_.left), y = static_cast<float>(rcNotice_.top);
  const float w = static_cast<float>(rcNotice_.right - rcNotice_.left), h = static_cast<float>(rcNotice_.bottom - rcNotice_.top);
  cv.FillRoundRect(x, y, w, h, static_cast<float>(S(10)), tint);
  cv.StrokeRoundRect(x, y, w, h, static_cast<float>(S(10)), static_cast<float>(dpi_) / 96.f, col, .45f);
  cv.FillRoundRect(x, y, static_cast<float>(S(5)), h, static_cast<float>(S(2)), col);
  const int tx = rcNotice_.left + S(18), tw = (rcNotice_.right - rcNotice_.left) - S(18) - S(14);
  const int th = CalcTextHeight(cv, fonts_.small, n.text, tw);
  const int maxH = rcNotice_.bottom - rcNotice_.top - S(8);
  const int use = th > maxH ? maxH : th;
  RECT tr = {tx, rcNotice_.top + ((rcNotice_.bottom - rcNotice_.top) - use) / 2, tx + tw, rcNotice_.top + ((rcNotice_.bottom - rcNotice_.top) + use) / 2};
  cv.Text(fonts_.small, n.text, tr, ui::kText, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
}

void MainWindow::PaintCard(ui::Canvas& cv) {
  const float x = static_cast<float>(rcCard_.left), y = static_cast<float>(rcCard_.top);
  const float w = static_cast<float>(rcCard_.right - rcCard_.left), h = static_cast<float>(rcCard_.bottom - rcCard_.top);
  cv.FillRoundRect(x, y, w, h, static_cast<float>(S(14)), ui::kCard);
  cv.StrokeRoundRect(x, y, w, h, static_cast<float>(S(14)), static_cast<float>(dpi_) / 96.f, ui::kBorder);

  // ---- sol: aktif profil
  std::wstring big = L"—", sub = txt::kChipReading();
  COLORREF bigCol = ui::kTextFaint, subCol = ui::kTextDim;
  if (haveState_) {
    if (cur_.activeProfile) {
      big = std::to_wstring(cur_.activeProfile);
      bigCol = ui::kText;
      sub = txt::kCardAgree();
      subCol = ui::kInfo;
    } else if (cur_.ec.ok && cur_.ecProfile) {
      big = std::to_wstring(cur_.ecProfile);
      bigCol = ui::kWarn;
      sub = cur_.nv.ok ? txt::kCardDisagree() : txt::kCardNoNv();
      subCol = ui::kWarn;
    } else if (cur_.ec.ok) {
      big = L"?";
      bigCol = ui::kWarn;
      sub = txt::kCardUnknownEc();
      subCol = ui::kWarn;
    } else {
      sub = cur_.identity.verdict != IdentityVerdict::Exact ? txt::kCardUnverifiedDevice() : txt::kCardEcUnreadable();
      subCol = ui::kWarn;
    }
  }
  if (applyInFlight_) {
    sub = txt::kChipApplying();
    subCol = ui::kAccentHi;
  }
  const int lx = rcCard_.left + S(22), ty = rcCard_.top + S(16);
  RECT r1 = {lx, ty, lx + S(190), ty + S(16)};
  cv.Text(fonts_.tinyBold, txt::kCardActive(), r1, ui::kTextDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
  const SIZE sz = cv.Measure(fonts_.watt, big);
  RECT r2 = {lx, rcCard_.top + S(32), lx + sz.cx + 4, rcCard_.top + S(32) + sz.cy};
  cv.Text(fonts_.watt, big, r2, bigCol, DT_LEFT | DT_TOP | DT_SINGLELINE);
  if (big != L"—" && big != L"?") {
    RECT r3 = {lx + sz.cx + S(4), rcCard_.top + S(32), lx + sz.cx + S(60), rcCard_.top + S(32) + sz.cy - S(10)};
    cv.Text(fonts_.wattUnit, L"W", r3, ui::kTextDim, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
  }
  RECT r4 = {lx, rcCard_.top + S(32) + sz.cy + S(2), lx + S(200), rcCard_.top + S(32) + sz.cy + S(22)};
  cv.Text(fonts_.small, sub, r4, subCol, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

  // ---- ayırıcı
  const float sepX = static_cast<float>(rcCard_.left + S(236));
  cv.Line(sepX, y + static_cast<float>(S(18)), sepX, y + static_cast<float>(S(120)), static_cast<float>(dpi_) / 96.f, ui::kBorder);

  // ---- sağ: ölçümler
  struct Cell {
    const wchar_t* label;
    std::wstring value;
    COLORREF col;
  } cells[6];
  const bool hv = haveState_;
  cells[0] = {txt::kCellNvLimit(), (hv && cur_.nv.ok) ? txt::Num1(cur_.nv.limitW) + L" W" : L"—", ui::kText};
  cells[1] = {txt::kCellEc(), (hv && cur_.ec.ok) ? Hex2W(cur_.ec.ctrl) + L" / " + std::to_wstring(cur_.ec.offset) + L" W" : L"—", ui::kText};
  cells[2] = {txt::kCellDraw(), (hv && cur_.nv.haveDraw) ? txt::Num1(cur_.nv.drawW) + L" W" : L"—", ui::kText};
  cells[3] = {txt::kCellTemp(), (hv && cur_.nv.haveTemp) ? std::to_wstring(cur_.nv.tempC) + L" °C" : L"—",
              (hv && cur_.nv.haveTemp && cur_.nv.tempC >= kTempWarnC) ? ui::kWarn : ui::kText};
  cells[4] = {txt::kCellUtil(), (hv && cur_.nv.haveUtil) ? std::to_wstring(cur_.nv.utilPct) + L" %" : L"—", ui::kText};
  cells[5] = {txt::kCellVbios(), (hv && cur_.nvInitOk && cur_.nvStatic.ok && cur_.nvStatic.maxW > 0) ? txt::Num1(cur_.nvStatic.maxW) + L" W" : L"—", ui::kText};
  const int gx = rcCard_.left + S(258), colW = (rcCard_.right - S(18) - gx) / 3;
  for (int i = 0; i < 6; ++i) {
    const int cx = gx + (i % 3) * colW, cy = rcCard_.top + S(20) + (i / 3) * S(52);
    RECT rl = {cx, cy, cx + colW - S(6), cy + S(16)};
    cv.Text(fonts_.tiny, cells[i].label, rl, ui::kTextDim, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT rv = {cx, cy + S(17), cx + colW - S(6), cy + S(40)};
    cv.Text(fonts_.semibold, cells[i].value, rv, cells[i].value == L"—" ? ui::kTextFaint : cells[i].col, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
  }

  // ---- alt şerit
  cv.Line(x + static_cast<float>(S(20)), y + static_cast<float>(S(122)), x + w - static_cast<float>(S(20)), y + static_cast<float>(S(122)),
          static_cast<float>(dpi_) / 96.f, ui::kBorder);
  std::wstring strip = txt::kStripDriver();
  strip += haveState_ ? txt::DriverTextFor(cur_) : L"…";
  strip += L"   •   ";
  strip += txt::kStripIdentity();
  strip += haveState_ ? txt::IdentityText(cur_.identity.verdict) : L"…";
  if (haveState_ && cur_.nvInitOk && cur_.nvStatic.ok && !cur_.nvStatic.name.empty()) strip += L"   •   " + Widen(cur_.nvStatic.name);
  RECT rs = {rcCard_.left + S(22), rcCard_.top + S(126), rcCard_.right - S(22), rcCard_.bottom - S(6)};
  cv.Text(fonts_.tiny, strip, rs, ui::kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void MainWindow::PaintAll(ui::Canvas& cv) {
  cv.Clear(ui::kBg);

  // ---- başlık
  cv.FillRoundRect(static_cast<float>(S(24)), static_cast<float>(S(18)), static_cast<float>(S(5)), static_cast<float>(S(34)),
                   static_cast<float>(S(2)), ui::kAccent);
  RECT rt = {S(40), S(14), S(460), S(46)};
  cv.Text(fonts_.title, txt::kAppTitle, rt, ui::kText, DT_LEFT | DT_TOP | DT_SINGLELINE);
  const bool demo = Backends::IsDemoBuild();
  RECT rsub = {S(40), S(46), S(520), S(66)};
  cv.Text(fonts_.tiny, demo ? txt::kHeaderSubDemo() : txt::kHeaderSub(),
          rsub, demo ? ui::kWarn : ui::kTextDim, DT_LEFT | DT_TOP | DT_SINGLELINE);

  // durum çipi
  std::wstring chip = txt::kChipReading();
  COLORREF chipCol = ui::kTextDim;
  if (applyInFlight_) {
    chip = txt::kChipApplying();
    chipCol = ui::kAccentHi;
  } else if (haveState_ && cur_.CanWrite()) {
    chip = txt::kChipReady();
    chipCol = ui::kInfo;
  } else if (haveState_) {
    chip = txt::kChipWriteOff();
    chipCol = txt::FirstBlockerNotice(cur_).level == Level::Error ? ui::kErr : ui::kWarn;
  }
  const SIZE cs = cv.Measure(fonts_.small, chip);
  const int chipW = cs.cx + S(36), chipH = S(28), chipX = rcLang_.left - S(10) - chipW, chipY = S(22);
  cv.FillRoundRect(static_cast<float>(chipX), static_cast<float>(chipY), static_cast<float>(chipW), static_cast<float>(chipH),
                   static_cast<float>(chipH) / 2.f, ui::kCard);
  cv.StrokeRoundRect(static_cast<float>(chipX), static_cast<float>(chipY), static_cast<float>(chipW), static_cast<float>(chipH),
                     static_cast<float>(chipH) / 2.f, static_cast<float>(dpi_) / 96.f, ui::kBorder);
  cv.FillCircle(static_cast<float>(chipX + S(14)), static_cast<float>(chipY + chipH / 2), static_cast<float>(S(4)), chipCol);
  RECT rc = {chipX + S(26), chipY, chipX + chipW - S(8), chipY + chipH};
  cv.Text(fonts_.small, chip, rc, ui::kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  PaintCard(cv);
  PaintNotice(cv);
  PaintPersist(cv);

  const wchar_t* descs[2] = {txt::kAutostartDesc(), txt::kRestoreDesc()};
  for (int i = 0; i < 2; ++i)
    cv.Text(fonts_.tiny, descs[i], rcDesc_[i], ui::kTextDim, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
  const std::wstring foot = std::wstring(L"v") + txt::kVersion + L"  ·  " + txt::kFooterTail();
  cv.Text(fonts_.tiny, foot, rcFooter_, ui::kTextFaint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void MainWindow::PaintPersist(ui::Canvas& cv) {
  const float x = static_cast<float>(rcPersist_.left), y = static_cast<float>(rcPersist_.top);
  const float w = static_cast<float>(rcPersist_.right - rcPersist_.left), h = static_cast<float>(rcPersist_.bottom - rcPersist_.top);
  const float line = static_cast<float>(dpi_) / 96.f;
  const PersistView v = CurrentPersistView();
  const bool available = v.kind != PersistKind::Locked && v.kind != PersistKind::Checking;
  cv.FillRoundRect(x, y, w, h, static_cast<float>(S(14)), ui::kCard);
  if (available) cv.StrokeRoundRect(x, y, w, h, static_cast<float>(S(14)), line, ui::kAccent, .55f);
  else cv.StrokeRoundRect(x, y, w, h, static_cast<float>(S(14)), line, ui::kBorder);

  // Başlık + açıklama HER ZAMAN görünür: kalıcılığın ne olduğunu ve neye bağlı olduğunu söyler.
  cv.Text(fonts_.tinyBold, txt::kPersistTitle(), rcPersistTitle_, ui::kTextDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
  cv.Text(fonts_.tiny, txt::kPersistDesc(), rcPersistDesc_, ui::kTextDim, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
  cv.Line(x + static_cast<float>(S(16)), static_cast<float>(persistSepY_), x + w - static_cast<float>(S(16)),
          static_cast<float>(persistSepY_), line, ui::kBorder);

  // Durum satırı: gerçek OEM dosyası + gerçek anlık ölçüm.
  const Notice st = PersistStatusNotice();
  COLORREF dot = ui::kInfo;
  if (st.level == Level::Warn) dot = ui::kWarn;
  else if (st.level == Level::Error) dot = ui::kErr;
  // Metin 1 ya da 2 satır olabilir; ayrılan bant içinde dikey ortalanır (tek satırda alt tarafta boş şerit kalmaz).
  const int tw = (rcPersistStatus_.right - rcPersistStatus_.left) - S(16);
  const int bandH = rcPersistStatus_.bottom - rcPersistStatus_.top;
  const int th = std::min(CalcTextHeight(cv, fonts_.small, st.text, tw), bandH);
  const int ty = rcPersistStatus_.top + (bandH - th) / 2;
  cv.FillCircle(static_cast<float>(rcPersistStatus_.left + S(5)), static_cast<float>(ty + S(8)), static_cast<float>(S(4)), dot);
  RECT rt = {rcPersistStatus_.left + S(16), ty, rcPersistStatus_.right, ty + th};
  cv.Text(fonts_.small, st.text, rt, ui::kText, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
}

// ======================================================================= denetim durumu
void MainWindow::UpdateControls() {
  if (!hwnd_) return;
  const bool canWrite = haveState_ && cur_.CanWrite();
  for (int i = 0; i < 6; ++i) {
    int flags = 0;
    const int w = kProfiles[i].watts;
    if (haveState_ && cur_.activeProfile == w) flags |= ui::kFlagActive;
    if (applyInFlight_ && applyTarget_ == w) flags |= ui::kFlagBusy;
    if (!canWrite || (applyInFlight_ && applyTarget_ != w)) flags |= ui::kFlagDim;
    SendMessageW(prof_[i], ui::MGPM_SETFLAGS, static_cast<WPARAM>(flags), 0);
  }
  // Açılışta başlat anahtarı GERÇEK görevden türetilir; işlem sürerken kullanıcının istediği duruma gider ve soluk çizilir.
  const bool autoOn = autostartPending_ >= 0 ? autostartPending_ != 0 : autostartOn_;
  SendMessageW(chkAutostart_, ui::MGPM_SETCHECK, autoOn ? 1 : 0, 0);
  SendMessageW(chkAutostart_, ui::MGPM_SETFLAGS, taskOpInFlight_ ? ui::kFlagDim : 0, 0);
  SendMessageW(chkRestore_, ui::MGPM_SETCHECK, settings_.restoreOnExit ? 1 : 0, 0);

  // Kartın durum satırı ve sonuç bildirimi değişince bir kez günlüğe yaz (kullanıcı desteği için; yalnız metin, EC/OEM'e dokunmaz).
  const std::wstring st = PersistStatusNotice().text;
  if (st != lastPersistStatus_) {
    lastPersistStatus_ = st;
    log_.Write(LogLevel::Info, "UI persist status: " + Narrow(st));
  }
  if (resultNotice_.text != lastLoggedNotice_) {
    lastLoggedNotice_ = resultNotice_.text;
    if (!lastLoggedNotice_.empty()) {
      log_.Write(resultNotice_.level == Level::Error  ? LogLevel::Error
                 : resultNotice_.level == Level::Warn ? LogLevel::Warn
                                                      : LogLevel::Info,
                 "UI notice: " + Narrow(lastLoggedNotice_));
    }
  }
}

void MainWindow::RearmPoll() {
  if (!hwnd_ || exiting_) return;
  KillTimer(hwnd_, IDT_POLL);
  if (IsShown()) SetTimer(hwnd_, IDT_POLL, 1500, nullptr);
  // Pencere gizliyken (tepsi) hiçbir zamanlayıcı ve hiçbir okuma yoktur.
}

void MainWindow::RequestPoll(bool forceFull, bool userRefresh) {
  if (!hwnd_ || exiting_ || pollInFlight_ || applyInFlight_) return;
  pollInFlight_ = true;
  if (forceFull) pollCount_ = 0;
  const bool light = haveState_ && (++pollCount_ % 30) != 0;
  const bool refreshId = forceFull;
  // OEM profil dosyası (salt okunur denetim): açılışta, kullanıcı/pencere yenilemesinde ve pencere açıkken tam okumalarda.
  // Dış değişiklikler (Control Center / OEM güncellemesi) kartın durum satırında görünür; hiçbir şey otomatik yazılmaz.
  if (forceFull || (!light && IsShown())) RequestOemInspect();
  HWND hw = hwnd_;
  Backends* be = &be_;
  worker_.Post([hw, be, light, refreshId, userRefresh]() {
    LiveState* s = nullptr;
    try {
      if (refreshId) be->engine().RefreshIdentity();
      s = new LiveState(be->engine().ReadLive(light, userRefresh));
    } catch (...) {
      // İstisna asla "okuma sürüyor" durumunda takılı bırakmaz: yazmayı kapatan bir durum bildirilir.
      delete s;
      s = new LiveState();
      s->blockers.push_back(Blocker::EcReadInvalid);
      s->blockerDetails.push_back("internal exception while reading state");
    }
    if (!PostMessageW(hw, WM_STATE, 0, reinterpret_cast<LPARAM>(s))) delete s;
  });
}

void MainWindow::OnState(LiveState* sp) {
  std::unique_ptr<LiveState> s(sp);
  pollInFlight_ = false;
  if (exiting_ || !s) return;
  cur_ = *s;
  haveState_ = true;
  // Doğrulanamayan geri alma: kritik uyarı kalıcıdır (motor yazma kilidini de tutar).
  if (cur_.HasBlocker(Blocker::FaultLatched)) SetNotice([]() { return Notice{Level::Error, txt::BlockerText(Blocker::FaultLatched)}; });
  UpdateTrayTip();
  Refresh();
}

// ======================================================================= komutlar
void MainWindow::OnCommand(int id) {
  if (id >= IDC_PROF0 && id < IDC_PROF0 + 6) {
    OnProfileClick(id - IDC_PROF0);
    return;
  }
  switch (id) {
    case IDC_CHK_AUTOSTART:
      OnAutostartToggle(SendMessageW(chkAutostart_, ui::MGPM_GETCHECK, 0, 0) != 0);
      break;
    case IDC_CHK_RESTORE:
      settings_.restoreOnExit = SendMessageW(chkRestore_, ui::MGPM_GETCHECK, 0, 0) != 0;
      SaveSettings(settings_);
      log_.Write(LogLevel::Info, std::string("UI: restore-on-exit ") + (settings_.restoreOnExit ? "ON" : "OFF"));
      Refresh();
      break;
    case IDC_BTN_REFRESH:
      ClearNotice();
      RequestPoll(true, true);
      Refresh();
      break;
    case IDC_BTN_CONSENT:
      OnResetConsents();
      break;
    case IDC_BTN_ABOUT:
      OnAbout();
      break;
    case IDC_LANG:
      OnLanguageToggle();
      break;
  }
}

// "Hakkında": yalnız kullanıcı tıklayınca varsayılan tarayıcıda proje sayfasını açar. Uygulamanın kendisi hiçbir ağ bağlantısı kurmaz.
void MainWindow::OnAbout() {
  const HINSTANCE r = ShellExecuteW(hwnd_, L"open", txt::kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(r) <= 32) {
    SetNotice([]() { return txt::AboutFailedNotice(); });
    log_.Write(LogLevel::Warn, "UI: the project page could not be opened in the default browser");
    Refresh();
  } else {
    log_.Write(LogLevel::Info, "UI: opened the project page in the default browser");
  }
}

// Sonuç bildirimi: üretici işlev saklanır; dil değişince metin yeni dilde yeniden üretilir.
void MainWindow::SetNotice(std::function<Notice()> mk) {
  noticeMk_ = std::move(mk);
  resultNotice_ = noticeMk_ ? noticeMk_() : Notice();
}
void MainWindow::ClearNotice() {
  noticeMk_ = nullptr;
  resultNotice_ = Notice();
}

// EN | TR düğmesi: dil anında değişir (yeniden başlatma gerekmez) ve seçim kaydedilir. Seçim yalnız metinleri etkiler; EC/OEM akışına dokunmaz.
void MainWindow::OnLanguageToggle() {
  const Lang want = SendMessageW(btnLang_, ui::MGPM_GETCHECK, 0, 0) ? Lang::En : Lang::Tr;
  if (want == CurrentLang()) return;
  ApplyLanguage(want);
  settings_.language = LangCode(want);
  settings_.languageInstaller = InstallerLanguageCode();  // seçim, o andaki kurulum diliyle damgalanır (bkz. core/language.h)
  if (!SaveSettings(settings_)) log_.Write(LogLevel::Warn, "UI: the language choice could not be saved to settings.ini");
  log_.Write(LogLevel::Info, std::string("UI: language switched to ") + LangCode(want));
}

void MainWindow::ApplyLanguage(Lang l) {
  SetCurrentLang(l);
  SendMessageW(btnLang_, ui::MGPM_SETCHECK, l == Lang::En ? 1 : 0, 0);
  SetWindowTextW(chkAutostart_, txt::kAutostartLabel());
  SetWindowTextW(chkRestore_, txt::kRestoreLabel());
  SetWindowTextW(btnRefresh_, txt::kRefresh());
  SetWindowTextW(btnConsent_, txt::kResetConsents());
  SetWindowTextW(btnAbout_, txt::kAboutLabel());
  SetWindowTextW(btnLang_, txt::kLangToggleName());
  for (int i = 0; i < 6; ++i) SendMessageW(prof_[i], ui::MGPM_SETSUB, 0, reinterpret_cast<LPARAM>(txt::ProfileSub(i)));
  SetWindowTextW(hwnd_, Backends::IsDemoBuild() ? txt::kWindowTitleDemo() : txt::kAppTitle);
  if (noticeMk_) resultNotice_ = noticeMk_();  // son işlem sonucu yeni dilde yeniden üretilir
  UpdateTrayTip();
  // Açıklama metinlerinin satır sayısı dile göre değişir: yerleşim ve pencere yüksekliği yeniden hesaplanır.
  ResizeForDpi(false);
  Refresh();
}

// ======================================================================= tepsi
// Tepsi simgesini ekler (ya da zaten varsa ipucunu günceller). Başarısızlık (görev çubuğu henüz hazır değil: oturum açılışında /minimized
// ile başlayan uygulamada olağandır) SESSİZCE yutulmaz: simge yokken pencere de gizliyse uygulamaya hiç ulaşılamazdı. Bu yüzden sınırlı
// yeniden deneme kurulur; hiç eklenemezse pencere gösterilir (bkz. OnTrayRetry).
bool MainWindow::AddTray() {
  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof nid;
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TRAY;
  nid.hIcon = iconSmall_;
  std::wstring tip = txt::kAppTitle;
  if (haveState_ && cur_.activeProfile) tip += L" — " + txt::Watts(cur_.activeProfile);
  std::wcsncpy(nid.szTip, tip.c_str(), 127);
  bool ok = false;
#ifdef MGP_DEMO
  // YALNIZ demo: MGP_DEMO_TRAYFAIL=N ilk N ekleme denemesini başarısız sayar (yeniden deneme / pencereyi gösterme yedeğini denemek için).
  static int demoFail = -1;
  if (demoFail < 0) {
    wchar_t b[16] = {0};
    demoFail = GetEnvironmentVariableW(L"MGP_DEMO_TRAYFAIL", b, 16) ? _wtoi(b) : 0;
  }
  if (demoFail > 0 && !trayShown_) {
    --demoFail;
    trayShown_ = false;
    if (!exiting_ && hwnd_ && !IsShown()) ArmTrayRetry();
    return false;
  }
#endif
  if (trayShown_) {
    ok = Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
    if (!ok) ok = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;  // Explorer çökmüş/yeniden başlamış: simge yok, yeniden ekle
  } else {
    ok = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (!ok) ok = Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;  // NIM_ADD zaman aşımına uğradı ama simge eklenmiş olabilir
  }
  trayShown_ = ok;
  if (ok) {
    trayRetries_ = 0;
    KillTimer(hwnd_, IDT_TRAYRETRY);
  } else if (!exiting_ && hwnd_ && !IsShown()) {
    ArmTrayRetry();
  }
  return ok;
}

void MainWindow::ArmTrayRetry() {
  if (trayRetries_ >= kTrayRetryMax) return;
  SetTimer(hwnd_, IDT_TRAYRETRY, kTrayRetryMs, nullptr);
}

void MainWindow::OnTrayRetry() {
  KillTimer(hwnd_, IDT_TRAYRETRY);
  if (exiting_ || IsShown() || trayShown_) return;  // pencere görünür oldu ya da simge zaten var: yapılacak iş yok
  const int attempt = ++trayRetries_;  // AddTray başarıda sayacı sıfırlar; günlük için önce yakalanır
  if (AddTray()) {
    log_.Write(LogLevel::Info, "tray icon added after " + std::to_string(attempt) + " retries");
    return;
  }
  if (attempt >= kTrayRetryMax) {
    // Simge eklenemiyor ve pencere gizli: uygulama ulaşılamaz kalmasın diye pencere gösterilir.
    log_.Write(LogLevel::Warn, "tray icon could not be added after repeated retries; showing the window instead");
    SetNotice([]() { return txt::TrayFailedNotice(); });
    trayRetries_ = 0;
    RestoreFromTray();
  }
}

// Yükseltilmiş (yönetici) süreç, orta bütünlükteki Explorer'ın gönderdiği "TaskbarCreated" yayınını UIPI nedeniyle ALAMAZ; izin verilmezse
// oturum açılışında görev çubuğu geç hazır olduğunda ya da Explorer yeniden başladığında tepsi simgesi geri gelmez.
void MainWindow::AllowShellMessages() {
  typedef BOOL(WINAPI * Fn)(HWND, UINT, DWORD, void*);
  HMODULE u = GetModuleHandleW(L"user32.dll");
  Fn f = u ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(u, "ChangeWindowMessageFilterEx"))) : nullptr;
  if (f && taskbarCreated_ && !f(hwnd_, taskbarCreated_, 1 /*MSGFLT_ALLOW*/, nullptr))
    log_.Write(LogLevel::Warn, "could not allow the TaskbarCreated message for the elevated window");
}

void MainWindow::RemoveTray() {
  if (!trayShown_) return;
  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof nid;
  nid.hWnd = hwnd_;
  nid.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  trayShown_ = false;
}

void MainWindow::UpdateTrayTip() {
  if (trayShown_) AddTray();  // NIM_MODIFY
}

void MainWindow::HideToTray() {
  ShowWindow(hwnd_, SW_HIDE);
  AddTray();
  RearmPoll();
}

void MainWindow::RestoreFromTray() {
  ShowWindow(hwnd_, SW_SHOW);
  if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
  SetForegroundWindow(hwnd_);
  KillTimer(hwnd_, IDT_TRAYRETRY);
  trayRetries_ = 0;
  RemoveTray();
  RequestPoll(true);
  RearmPoll();
  Refresh();
}

void MainWindow::ShowMe() {
  if (!IsShown()) RestoreFromTray();
  else SetForegroundWindow(hwnd_);
}

void MainWindow::ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, IDM_SHOW, txt::kMenuShow());
  const std::wstring info = txt::MenuActive((haveState_ && cur_.activeProfile) ? cur_.activeProfile : 0);
  AppendMenuW(menu, MF_STRING | MF_GRAYED, IDM_INFO, info.c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_EXIT, txt::kMenuExit());
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (cmd == IDM_SHOW) RestoreFromTray();
  else if (cmd == IDM_EXIT) RequestExit();
}

// ======================================================================= çıkış
void MainWindow::RequestExit() {
  if (exiting_) return;
  if (applyInFlight_) {
    exitPending_ = true;  // yazma sürerken kapatılmaz (EC ya da OEM profil dosyası); bitince çıkılır
    SetNotice([]() { return txt::ExitWaitNotice(); });
    Refresh();
    return;
  }
  if (settings_.restoreOnExit && haveState_ && cur_.CanWrite() && cur_.activeProfile != kOemMaxWatts) {
    log_.Write(LogLevel::Info, "exit: restoring OEM 140 W (live only; the OEM profile file is not touched)");
    SetTimer(hwnd_, IDT_EXITGUARD, 60000, nullptr);
    SetNotice([]() { return txt::ExitRestoringNotice(); });
    exitPending_ = false;
    StartApply(kOemMaxWatts, true);
    return;
  }
  DoExit();
}

void MainWindow::DoExit(DWORD workerWaitMs) {
  if (exiting_) return;
  exiting_ = true;
  KillTimer(hwnd_, IDT_POLL);
  KillTimer(hwnd_, IDT_EXITGUARD);
  KillTimer(hwnd_, IDT_RESUME);
  KillTimer(hwnd_, IDT_TRAYRETRY);
  RemoveTray();
  SaveSettings(settings_);
  if (!worker_.Stop(workerWaitMs)) {
    // Arka plan iş parçacığı yanıt vermeyen bir sürücü çağrısında olabilir ve iptal edilemez: süreç sonlandırılır.
    log_.Write(LogLevel::Error, "worker still busy after bounded wait; terminating process");
    ExitProcess(3);
  }
  log_.Write(LogLevel::Info, "exit");
  DestroyWindow(hwnd_);
}

}  // namespace win
}  // namespace mgp

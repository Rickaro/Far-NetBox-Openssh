
// NetBoxAskpass - minimal SSH_ASKPASS helper for the OpenSSH backend.
//
// OpenSSH invokes this executable when it needs a password, a key passphrase
// or a host key confirmation. The plugin passes a stored password through the
// NETBOX_ASKPASS_PASSWORD environment variable; otherwise a small native
// dialog is shown. The answer is written to stdout, which ssh.exe reads.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <string>
#include <vector>

namespace
{
constexpr int32_t DialogWidth = 430;
constexpr int32_t DialogHeight = 170;
constexpr int32_t EditId = 1001;
constexpr int32_t OkId = IDOK;
constexpr int32_t CancelId = IDCANCEL;

HWND DialogWindow = nullptr;
HWND PromptLabel = nullptr;
HWND EditBox = nullptr;
std::wstring Answer;
bool Confirmed = false;
bool HostKeyPrompt = false;

std::wstring Trim(const std::wstring & Value)
{
  const wchar_t * const Spaces = L" \t\r\n";
  const size_t First = Value.find_first_not_of(Spaces);
  if (First == std::wstring::npos)
  {
    return std::wstring();
  }
  const size_t Last = Value.find_last_not_of(Spaces);
  return Value.substr(First, Last - First + 1);
}

bool ContainsNoCase(const std::wstring & Value, const wchar_t * Needle)
{
  if (Value.empty() || (Needle == nullptr) || (*Needle == L'\0'))
  {
    return false;
  }
  std::wstring LowerValue(Value);
  std::wstring LowerNeedle(Needle);
  ::CharLowerBuffW(&LowerValue[0], static_cast<DWORD>(LowerValue.size()));
  ::CharLowerBuffW(&LowerNeedle[0], static_cast<DWORD>(LowerNeedle.size()));
  return LowerValue.find(LowerNeedle) != std::wstring::npos;
}

void WriteAnswer(const std::wstring & Value)
{
  int32_t Size = ::WideCharToMultiByte(CP_UTF8, 0, Value.c_str(),
    static_cast<int32_t>(Value.size()), nullptr, 0, nullptr, nullptr);
  std::string Utf8(static_cast<size_t>(Size), '\0');
  if (Size > 0)
  {
    ::WideCharToMultiByte(CP_UTF8, 0, Value.c_str(),
      static_cast<int32_t>(Value.size()), &Utf8[0], Size, nullptr, nullptr);
  }
  Utf8.push_back('\n');

  const HANDLE Output = ::GetStdHandle(STD_OUTPUT_HANDLE);
  if ((Output != nullptr) && (Output != INVALID_HANDLE_VALUE))
  {
    DWORD Written = 0;
    ::WriteFile(Output, Utf8.data(), static_cast<DWORD>(Utf8.size()), &Written, nullptr);
  }
}

void LayoutControls(HWND Window)
{
  RECT Client{};
  ::GetClientRect(Window, &Client);
  const int32_t Width = Client.right - Client.left;
  const int32_t Height = Client.bottom - Client.top;

  ::MoveWindow(PromptLabel, 12, 12, Width - 24, 60, TRUE);
  ::MoveWindow(EditBox, 12, 78, Width - 24, 24, TRUE);
  ::MoveWindow(::GetDlgItem(Window, OkId), Width - 190, Height - 38, 82, 26, TRUE);
  ::MoveWindow(::GetDlgItem(Window, CancelId), Width - 98, Height - 38, 82, 26, TRUE);
}

LRESULT CALLBACK DialogProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
  switch (Message)
  {
    case WM_CREATE:
      DialogWindow = Window;
      return 0;

    case WM_COMMAND:
      if ((LOWORD(WParam) == OkId) || (LOWORD(WParam) == CancelId))
      {
        if (LOWORD(WParam) == OkId)
        {
          const int32_t Length = ::GetWindowTextLengthW(EditBox);
          std::vector<wchar_t> Buffer(static_cast<size_t>(Length) + 1, L'\0');
          if (Length > 0)
          {
            ::GetWindowTextW(EditBox, Buffer.data(), Length + 1);
          }
          Answer.assign(Buffer.data(), static_cast<size_t>(Length));
          Confirmed = true;
        }
        ::DestroyWindow(Window);
        return 0;
      }
      break;

    case WM_CLOSE:
      ::DestroyWindow(Window);
      return 0;

    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return ::DefWindowProcW(Window, Message, WParam, LParam);
}

bool RunDialog(const std::wstring & Prompt)
{
  const HINSTANCE Instance = ::GetModuleHandleW(nullptr);
  const wchar_t * const ClassName = L"NetBoxAskpassWindow";

  WNDCLASSEXW WindowClass{};
  WindowClass.cbSize = sizeof(WindowClass);
  WindowClass.lpfnWndProc = DialogProc;
  WindowClass.hInstance = Instance;
  WindowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  WindowClass.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
  WindowClass.lpszClassName = ClassName;
  if (::RegisterClassExW(&WindowClass) == 0)
  {
    return false;
  }

  RECT Rect{0, 0, DialogWidth, DialogHeight};
  ::AdjustWindowRect(&Rect, WS_CAPTION | WS_SYSMENU, FALSE);
  const int32_t Width = Rect.right - Rect.left;
  const int32_t Height = Rect.bottom - Rect.top;

  const int32_t ScreenWidth = ::GetSystemMetrics(SM_CXSCREEN);
  const int32_t ScreenHeight = ::GetSystemMetrics(SM_CYSCREEN);
  const int32_t Left = (ScreenWidth - Width) / 2;
  const int32_t Top = (ScreenHeight - Height) / 2;

  const HWND Window = ::CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
    ClassName, L"NetBox - OpenSSH",
    WS_CAPTION | WS_SYSMENU | WS_POPUP,
    Left, Top, Width, Height, nullptr, nullptr, Instance, nullptr);
  if (Window == nullptr)
  {
    return false;
  }

  const HFONT Font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));

  PromptLabel = ::CreateWindowExW(0, L"STATIC", Prompt.c_str(),
    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
    12, 12, DialogWidth - 24, 60, Window, nullptr, Instance, nullptr);
  EditBox = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
    12, 78, DialogWidth - 24, 24, Window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(EditId)), Instance, nullptr);

  const HWND OkButton = ::CreateWindowExW(0, L"BUTTON", L"OK",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
    0, 0, 82, 26, Window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(OkId)), Instance, nullptr);
  const HWND CancelButton = ::CreateWindowExW(0, L"BUTTON", L"Cancel",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
    0, 0, 82, 26, Window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(CancelId)), Instance, nullptr);

  ::SendMessageW(PromptLabel, WM_SETFONT, reinterpret_cast<WPARAM>(Font), TRUE);
  ::SendMessageW(EditBox, WM_SETFONT, reinterpret_cast<WPARAM>(Font), TRUE);
  ::SendMessageW(OkButton, WM_SETFONT, reinterpret_cast<WPARAM>(Font), TRUE);
  ::SendMessageW(CancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(Font), TRUE);




  LayoutControls(Window);
  ::ShowWindow(Window, SW_SHOW);
  ::SetForegroundWindow(Window);
  ::SetFocus(EditBox);

  MSG Message{};
  while (::GetMessageW(&Message, nullptr, 0, 0) > 0)
  {
    if (Message.message == WM_KEYDOWN)
    {
      if (Message.wParam == VK_ESCAPE)
      {
        ::DestroyWindow(Window);
        break;
      }
      if (Message.wParam == VK_RETURN)
      {
        ::PostMessageW(Window, WM_COMMAND, static_cast<WPARAM>(OkId), 0);
      }
    }
    ::TranslateMessage(&Message);
    ::DispatchMessageW(&Message);
  }

  return Confirmed;
}
} // namespace

int wmain(int argc, wchar_t ** argv)
{
  const std::wstring Prompt = (argc > 1) ? argv[1] : L"Password:";
  HostKeyPrompt = ContainsNoCase(Prompt, L"yes/no") ||
    ContainsNoCase(Prompt, L"are you sure you want to continue connecting");

  if (HostKeyPrompt)
  {
    const int32_t Result = ::MessageBoxW(nullptr, Prompt.c_str(),
      L"NetBox - OpenSSH host key", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
    WriteAnswer(Result == IDYES ? L"yes" : L"no");
    return 0;
  }

  wchar_t Buffer[4096]{};
  const DWORD Length = ::GetEnvironmentVariableW(L"NETBOX_ASKPASS_PASSWORD",
    Buffer, static_cast<DWORD>(_countof(Buffer)));
  if (Length > 0)
  {
    WriteAnswer(std::wstring(Buffer, Length));
    return 0;
  }

  if (RunDialog(Trim(Prompt)))
  {
    WriteAnswer(Answer);
  }
  else
  {
    WriteAnswer(std::wstring());
  }
  return 0;
}

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <urlmon.h>
#include <memory>
#include <functional>
#include <thread>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup")

#define IDI_APP_ICON 101

#define MSG_INSTALLATION_COMPLETE (WM_USER + 1)

#define BTN_NEXT 101
#define BTN_PREV 102
#define BTN_ACCEPT 103
#define BTN_DECLINE 104
#define BTN_FINISH 105
#define BTN_LICENSE_7ZIP 106
#define BTN_LICENSE_XNVIEW 107

constexpr int WINDOW_WIDTH = 650;
constexpr int WINDOW_HEIGHT = 380;  
constexpr int MARGIN = 20;
constexpr int BUTTON_WIDTH = 100;
constexpr int BUTTON_HEIGHT = 30;
constexpr int PROGRESS_BAR_WIDTH = 450;

class AppManager {
private:
    std::wstring m_name;
    std::wstring m_description;
    std::wstring m_licenseUrl;
    std::wstring m_downloadUrl;
    std::wstring m_installArgs;
    std::wstring m_registryKey;
    bool m_accepted;
    bool m_installed;

public:
    AppManager(const std::wstring& name, const std::wstring& description,
        const std::wstring& licenseUrl, const std::wstring& downloadUrl,
        const std::wstring& installArgs, const std::wstring& registryKey)
        : m_name(name), m_description(description), m_licenseUrl(licenseUrl),
        m_downloadUrl(downloadUrl), m_installArgs(installArgs), m_registryKey(registryKey),
        m_accepted(false), m_installed(false) {
        checkInstallationStatus();
    }

    void checkInstallationStatus() {
        m_installed = IsAppInstalled(m_registryKey);
    }

    bool isInstalled() const { return m_installed; }
    bool isAccepted() const { return m_accepted; }
    void setAccepted(bool accepted) { m_accepted = accepted; }
    const std::wstring& getName() const { return m_name; }
    const std::wstring& getDescription() const { return m_description; }
    const std::wstring& getLicenseUrl() const { return m_licenseUrl; }

    bool downloadInstaller(const std::wstring& downloadsPath) {
        std::wstring tempPath = downloadsPath + L"\\" + m_name + L"_installer.exe";
        return URLDownloadToFile(NULL, m_downloadUrl.c_str(), tempPath.c_str(), 0, NULL) == S_OK;
    }

    bool install(const std::wstring& downloadsPath) {
        std::wstring tempPath = downloadsPath + L"\\" + m_name + L"_installer.exe";

        if (!downloadInstaller(downloadsPath)) {
            return false;
        }

        SHELLEXECUTEINFO sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = tempPath.c_str();
        sei.lpParameters = m_installArgs.c_str();
        sei.nShow = SW_HIDE;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;

        if (!ShellExecuteEx(&sei)) {
            DWORD err = GetLastError();
            DeleteFile(tempPath.c_str());
            if (err == ERROR_CANCELLED) {
                return false;
            }
            return false;
        }

        WaitForSingleObject(sei.hProcess, INFINITE);

        DWORD exitCode;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);

        DeleteFile(tempPath.c_str());

        bool success = (exitCode == ERROR_SUCCESS) ||
            (exitCode == ERROR_SUCCESS_REBOOT_REQUIRED) ||
            (exitCode == 0);

        if (success) {
            Sleep(1000); 
            checkInstallationStatus();
        }

        return success;
    }

private:
    static bool IsAppInstalled(const std::wstring& registryKey) {
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, registryKey.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, registryKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        if (RegOpenKeyEx(HKEY_CURRENT_USER, registryKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        return false;
    }
};

class InstallerUI {
private:
    HWND m_hWnd;
    HWND m_hProgress;
    std::vector<AppManager> m_apps;
    std::wstring m_downloadsPath;
    bool m_silentMode;
    int m_currentAppIndex;

public:
    InstallerUI(HINSTANCE hInstance, const std::vector<AppManager>& apps,
        const std::wstring& downloadsPath, bool silentMode)
        : m_hWnd(nullptr), m_hProgress(nullptr), m_apps(apps),
        m_downloadsPath(downloadsPath), m_silentMode(silentMode), m_currentAppIndex(0) {

        initCommonControls();
        createWindow(hInstance);
    }

    ~InstallerUI() {
        if (m_hWnd) {
            DestroyWindow(m_hWnd);
        }
    }

    HWND getWindowHandle() const { return m_hWnd; }

    void showWelcomePage() {
        clearWindow();
        m_currentAppIndex = 0;

        createStaticText(MARGIN, MARGIN, getClientWidth() - 2 * MARGIN, 150,
            L"Добро пожаловать в установщик приложений.\n\n"
            L"Этот мастер установит выбранные вами приложения на ваш компьютер.\n"
            L"Нажмите 'Далее' для продолжения.");

        createButton(getRightButtonX(), getBottomButtonY(),
            BUTTON_WIDTH, BUTTON_HEIGHT, L"Далее", BTN_NEXT);
    }

    void showAppSelectionPage(int appIndex) {
        if (appIndex >= static_cast<int>(m_apps.size())) {
            startInstallation();
            return;
        }

        m_currentAppIndex = appIndex;
        clearWindow();

        if (m_apps[appIndex].isInstalled()) {
            showAppSelectionPage(appIndex + 1);
            return;
        }

        std::wstring title = m_apps[appIndex].getName() + L"\n\n";
        createStaticText(MARGIN, MARGIN, getClientWidth() - 2 * MARGIN, 120,
            (title + m_apps[appIndex].getDescription()).c_str());

        int linkWidth = 180;
        int linkY = getClientHeight() - 100;
        createHyperlink(getCenterX(linkWidth), linkY, linkWidth, 20, 
            L"Лицензионное соглашение", BTN_LICENSE_7ZIP + appIndex);

        int buttonsY = getClientHeight() - 60;
        createButton(getLeftButtonX(), buttonsY, BUTTON_WIDTH, BUTTON_HEIGHT, L"Принять", BTN_ACCEPT);
        createButton(getRightButtonX(), buttonsY, BUTTON_WIDTH, BUTTON_HEIGHT, L"Отклонить", BTN_DECLINE);
    }

    void showInstallationProgressPage() {
        clearWindow();

        createStaticText(getCenterX(PROGRESS_BAR_WIDTH), 50, PROGRESS_BAR_WIDTH, 50,
            L"Идет установка выбранных приложений...");

        m_hProgress = createProgressBar(getCenterX(PROGRESS_BAR_WIDTH), 120,
            PROGRESS_BAR_WIDTH, 30);
        SendMessage(m_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(m_hProgress, PBM_SETSTEP, 1, 0);

        startInstallationThread();
    }

    void showResultsPage() {
        clearWindow();

        std::wstring resultText = L"Результаты установки:\n\n";

        for (const auto& app : m_apps) {
            bool isInstalledNow = false;

            for (int i = 0; i < 5; i++) {
                AppManager tempApp = app;
                tempApp.checkInstallationStatus();
                if (tempApp.isInstalled()) {
                    isInstalledNow = true;
                    break;
                }
                Sleep(200);
            }

            resultText += app.getName() + L": ";
            if (app.isAccepted()) {
                if (isInstalledNow) {
                    resultText += L"✓ Успешно установлено\n";
                }
                else {
                    resultText += L"✗ Не удалось установить\n";
                }
            }
            else if (app.isInstalled()) {
                resultText += L"✓ Уже установлено\n";
            }
            else {
                resultText += L"✗ Установка отменена\n";
            }
        }

        createStaticText(MARGIN, MARGIN, getClientWidth() - 2 * MARGIN, 
                        getClientHeight() - 100, resultText.c_str());

        createButton(getCenterX(BUTTON_WIDTH), getBottomButtonY(),
            BUTTON_WIDTH, BUTTON_HEIGHT, L"Завершить", BTN_FINISH);
    }

    void handleCommand(int commandId) {
        if (commandId >= BTN_LICENSE_7ZIP && commandId < BTN_LICENSE_7ZIP + static_cast<int>(m_apps.size())) {
            int appIndex = commandId - BTN_LICENSE_7ZIP;
            if (appIndex < static_cast<int>(m_apps.size())) {
                ShellExecute(NULL, L"open", m_apps[appIndex].getLicenseUrl().c_str(),
                    NULL, NULL, SW_SHOW);
            }
            return;
        }

        switch (commandId) {
        case BTN_NEXT:
            showAppSelectionPage(0);
            break;

        case BTN_ACCEPT:
            if (m_currentAppIndex < static_cast<int>(m_apps.size())) {
                m_apps[m_currentAppIndex].setAccepted(true);
                showAppSelectionPage(m_currentAppIndex + 1);
            }
            break;

        case BTN_DECLINE:
            if (m_currentAppIndex < static_cast<int>(m_apps.size())) {
                m_apps[m_currentAppIndex].setAccepted(false);
                showAppSelectionPage(m_currentAppIndex + 1);
            }
            break;

        case BTN_FINISH:
            PostMessage(m_hWnd, WM_CLOSE, 0, 0);
            break;
        }
    }

private:
    int getClientWidth() const {
        RECT rcClient;
        if (m_hWnd && GetClientRect(m_hWnd, &rcClient)) {
            return rcClient.right - rcClient.left;
        }
        return WINDOW_WIDTH;
    }

    int getClientHeight() const {
        RECT rcClient;
        if (m_hWnd && GetClientRect(m_hWnd, &rcClient)) {
            return rcClient.bottom - rcClient.top;
        }
        return WINDOW_HEIGHT;
    }

    int getBottomButtonY() const {
        return getClientHeight() - MARGIN - BUTTON_HEIGHT;
    }

    int getLeftButtonX() const {
        return MARGIN;
    }

    int getRightButtonX() const {
        return getClientWidth() - MARGIN - BUTTON_WIDTH;
    }

    int getCenterX(int elementWidth) const {
        return (getClientWidth() - elementWidth) / 2;
    }

    int getCenterY(int elementHeight) const {
        return (getClientHeight() - elementHeight) / 2;
    }

    void initCommonControls() {
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_PROGRESS_CLASS;
        InitCommonControlsEx(&icex);
    }

    void createWindow(HINSTANCE hInstance) {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = InstallerUI::staticWndProc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
        if (!wc.hIcon) {
            wc.hIcon = (HICON)LoadImage(NULL, L"installer.ico", IMAGE_ICON,
                0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
            if (!wc.hIcon) {
                wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            }
        }

        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"InstallerWindowClass";

        wc.hIconSm = (HICON)LoadImage(NULL, L"app.ico", IMAGE_ICON,
            16, 16, LR_LOADFROMFILE);
        if (!wc.hIconSm) {
            wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
        }

        RegisterClassEx(&wc);

        RECT rcWindow = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
        AdjustWindowRect(&rcWindow, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        int windowWidth = rcWindow.right - rcWindow.left;
        int windowHeight = rcWindow.bottom - rcWindow.top;

        m_hWnd = CreateWindowEx(0, wc.lpszClassName, L"Установщик приложений",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
            NULL, NULL, hInstance, this);

        if (m_hWnd) {
            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
        }
    }

    void clearWindow() {
        if (!m_hWnd) return;

        HWND hChild = GetWindow(m_hWnd, GW_CHILD);
        while (hChild) {
            DestroyWindow(hChild);
            hChild = GetWindow(m_hWnd, GW_CHILD);
        }
        m_hProgress = nullptr;
    }

    HWND createStaticText(int x, int y, int width, int height, const wchar_t* text) {
        return CreateWindow(L"STATIC", text,
            WS_VISIBLE | WS_CHILD,
            x, y, width, height,
            m_hWnd, NULL, NULL, NULL);
    }

    HWND createButton(int x, int y, int width, int height, const wchar_t* text, int id) {
        return CreateWindow(L"BUTTON", text,
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x, y, width, height,
            m_hWnd, (HMENU)(UINT_PTR)id, NULL, NULL);
    }

    HWND createHyperlink(int x, int y, int width, int height, const wchar_t* text, int id) {
        HWND hLink = CreateWindow(L"STATIC", text,
            WS_VISIBLE | WS_CHILD | SS_NOTIFY,
            x, y, width, height,
            m_hWnd, (HMENU)(UINT_PTR)id, NULL, NULL);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        LOGFONT lf;
        GetObject(hFont, sizeof(lf), &lf);
        lf.lfUnderline = TRUE;
        HFONT hUnderlinedFont = CreateFontIndirect(&lf);
        SendMessage(hLink, WM_SETFONT, (WPARAM)hUnderlinedFont, TRUE);

        return hLink;
    }

    HWND createProgressBar(int x, int y, int width, int height) {
        return CreateWindowEx(0, PROGRESS_CLASS, NULL,
            WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
            x, y, width, height,
            m_hWnd, NULL, NULL, NULL);
    }

    void startInstallationThread() {
        std::thread([this]() {
            int totalApps = 0;
            int completedApps = 0;

            for (const auto& app : m_apps) {
                if (app.isAccepted() && !app.isInstalled()) {
                    totalApps++;
                }
            }

            if (totalApps == 0) {
                PostMessage(m_hWnd, MSG_INSTALLATION_COMPLETE, 0, 0);
                return;
            }

            SendMessage(m_hProgress, PBM_SETRANGE32, 0, totalApps * 100);

            for (auto& app : m_apps) {
                if (app.isAccepted() && !app.isInstalled()) {
                    int currentProgress = completedApps * 100;

                    for (int i = 0; i < 100; i += 10) {
                        SendMessage(m_hProgress, PBM_SETPOS, currentProgress + i, 0);
                        Sleep(50);
                    }

                    if (app.install(m_downloadsPath)) {
                        completedApps++;
                        SendMessage(m_hProgress, PBM_SETPOS, completedApps * 100, 0);
                    }
                }
            }

            PostMessage(m_hWnd, MSG_INSTALLATION_COMPLETE, 0, 0);
            }).detach();
    }

    void startInstallation() {
        if (m_silentMode) {
            for (auto& app : m_apps) {
                if (!app.isInstalled()) {
                    app.install(m_downloadsPath);
                }
            }
        }
        else {
            showInstallationProgressPage();
        }
    }

    static LRESULT CALLBACK staticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        InstallerUI* pThis = nullptr;

        if (message == WM_CREATE) {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            pThis = reinterpret_cast<InstallerUI*>(pCreate->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        else {
            pThis = reinterpret_cast<InstallerUI*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }

        if (pThis) {
            return pThis->instanceWndProc(hWnd, message, wParam, lParam);
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    LRESULT instanceWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_COMMAND:
            handleCommand(LOWORD(wParam));
            break;

        case MSG_INSTALLATION_COMPLETE:
            showResultsPage();
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        return 0;
    }
};

std::wstring getDownloadsPath() {
    PWSTR path = nullptr;
    std::wstring downloadsPath;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path))) {
        downloadsPath = path;
        CoTaskMemFree(path);
    }
    else {
        downloadsPath = L"C:\\Users\\Public\\Downloads";
    }

    return downloadsPath;
}

std::vector<AppManager> createAppList(const std::wstring& downloadsPath) {
    return {
        AppManager(
            L"7-Zip",
            L"7-Zip - архиватор файлов с высокой степенью сжатия.\n"
            L"Поддерживает множество форматов архивов и файловых систем.",
            L"https://www.7-zip.org/license.txt",
            L"https://www.7-zip.org/a/7z2501.exe",
            L"/S",
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\7-Zip"
        ),
        AppManager(
            L"XnView MP",
            L"XnView MP - мощный просмотрщик и конвертер изображений.\n"
            L"Поддерживает более 500 форматов изображений.",
            L"https://www.xnview.com/license.html",
            L"https://download.xnview.com/XnViewMP-win.exe",
            L"/VERYSILENT",
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\XnViewMP_is1"
        )
    };
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    bool silentMode = (strstr(lpCmdLine, "/S") != nullptr ||
        strstr(lpCmdLine, "/verysilent") != nullptr ||
        strstr(lpCmdLine, "/silent") != nullptr);

    std::wstring downloadsPath = getDownloadsPath();

    std::vector<AppManager> apps = createAppList(downloadsPath);

    if (silentMode) {
        for (auto& app : apps) {
            if (!app.isInstalled()) {
                app.install(downloadsPath);
            }
        }
        return 0;
    }

    InstallerUI installer(hInstance, apps, downloadsPath, silentMode);

    if (!installer.getWindowHandle()) {
        return 1;
    }

    installer.showWelcomePage();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
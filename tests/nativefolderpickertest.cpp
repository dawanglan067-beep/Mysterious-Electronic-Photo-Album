#include "albumfolderpicker.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QDir>
#include <QDebug>
#include <thread>
#include <atomic>
#define NOMINMAX
#include <windows.h>

struct Probe
{
    bool cancel;
    std::atomic_bool acted{false};
    std::atomic_bool done{false};
};

static BOOL CALLBACK ClickFolderButton(HWND child, LPARAM value)
{
    auto *probe = reinterpret_cast<Probe*>(value);
    wchar_t label[256] = {};
    wchar_t type[64] = {};
    GetWindowTextW(child, label, 256);
    GetClassNameW(child, type, 64);
    if (wcscmp(type, L"Button") == 0 && wcscmp(label, L"选择当前文件夹") == 0 && IsWindowEnabled(child)) {
        probe->acted = true;
        SendMessageW(child, BM_CLICK, 0, 0);
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK FindDialog(HWND window, LPARAM value)
{
    DWORD process = 0; GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window)) return TRUE;
    wchar_t type[64] = {}; GetClassNameW(window, type, 64);
    if (wcscmp(type, L"#32770") != 0) return TRUE;
    auto *probe = reinterpret_cast<Probe*>(value);
    if (probe->cancel) { probe->acted = true; PostMessageW(window, WM_CLOSE, 0, 0); }
    else EnumChildWindows(window, ClickFolderButton, value);
    return TRUE;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString folder = temporary.filePath(QStringLiteral("中文相册"));
    QDir().mkpath(folder);
    QImage image(100, 100, QImage::Format_RGB32); image.fill(Qt::cyan);
    if (!image.save(folder + QStringLiteral("/照片.png"))) return 1;
    int failures = 0;
    for (bool cancel : {false, true}) {
        Probe probe{cancel};
        std::thread automation([&]() {
            for (int i = 0; i < 100 && !probe.done; ++i) {
                Sleep(100);
                EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&probe));
                if (probe.acted) break;
            }
            if (!probe.acted && !probe.done) {
                probe.cancel = true;
                EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&probe));
            }
        });
        const auto selection = SelectAlbumFolder(nullptr, QStringLiteral("Album 原生目录选择验证"), folder);
        probe.done = true;
        automation.join();
        const bool passed = probe.acted && selection.error.isEmpty() && (cancel
            ? selection.path.isEmpty() : QDir::cleanPath(selection.path) == QDir::cleanPath(folder));
        qInfo() << (passed ? "PASS" : "FAIL") << (cancel ? "native cancellation" : "native current-folder selection with Chinese path")
                << selection.path << selection.error;
        if (!passed) ++failures;
    }
    return failures ? 1 : 0;
}

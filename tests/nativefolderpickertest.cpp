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
#include <UIAutomation.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
enum class Scenario { CurrentFolder, SelectedFolder, SelectedImage, Cancel };

struct Probe
{
    Scenario scenario;
    bool cancel = false;
    bool selected = false;
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
    const wchar_t *expected = probe->scenario == Scenario::CurrentFolder
        ? L"选择当前文件夹" : L"选择此文件夹";
    if (wcscmp(type, L"Button") == 0 && wcscmp(label, expected) == 0) {
        if (probe->scenario == Scenario::SelectedImage) {
            if (!IsWindowEnabled(child)) {
                probe->acted = true;
                PostMessageW(GetAncestor(child, GA_ROOT), WM_CLOSE, 0, 0);
                return FALSE;
            }
            return TRUE;
        }
        if (!IsWindowEnabled(child)) return TRUE;
        probe->acted = true;
        PostMessageW(child, BM_CLICK, 0, 0);
        return FALSE;
    }
    return TRUE;
}

static bool SelectItem(HWND window, const wchar_t *name)
{
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation)))) return false;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(window, &root))) return false;
    VARIANT value; VariantInit(&value);
    value.vt = VT_BSTR; value.bstrVal = SysAllocString(name);
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value);
    if (FAILED(result)) return false;
    ComPtr<IUIAutomationElementArray> matches;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), &matches))) return false;
    int length = 0; matches->get_Length(&length);
    for (int index = 0; index < length; ++index) {
        ComPtr<IUIAutomationElement> item;
        ComPtr<IUIAutomationSelectionItemPattern> selection;
        if (SUCCEEDED(matches->GetElement(index, &item))
            && SUCCEEDED(item->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection)))
            && SUCCEEDED(selection->Select())) return true;
    }
    return false;
}

static BOOL CALLBACK FindDialog(HWND window, LPARAM value)
{
    DWORD process = 0; GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window)) return TRUE;
    wchar_t type[64] = {}; GetClassNameW(window, type, 64);
    if (wcscmp(type, L"#32770") != 0) return TRUE;
    auto *probe = reinterpret_cast<Probe*>(value);
    if (probe->cancel) { probe->acted = true; PostMessageW(window, WM_CLOSE, 0, 0); }
    else {
        if (probe->scenario == Scenario::SelectedFolder || probe->scenario == Scenario::SelectedImage) {
            if (!probe->selected) {
                probe->selected = SelectItem(window, probe->scenario == Scenario::SelectedFolder
                    ? L"Screenshots" : L"sample.png");
                // File extensions may be hidden by the Windows shell.
                if (!probe->selected && probe->scenario == Scenario::SelectedImage)
                    probe->selected = SelectItem(window, L"sample");
                return TRUE;
            }
        }
        EnumChildWindows(window, ClickFolderButton, value);
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString folder = temporary.filePath(QStringLiteral("中文相册"));
    QDir().mkpath(folder);
    const QString childFolder = folder + QStringLiteral("/Screenshots");
    QDir().mkpath(childFolder);
    QImage image(100, 100, QImage::Format_RGB32); image.fill(Qt::cyan);
    if (!image.save(folder + QStringLiteral("/sample.png"))) return 1;
    int failures = 0;
    for (auto scenario : {Scenario::CurrentFolder, Scenario::SelectedFolder, Scenario::SelectedImage, Scenario::Cancel}) {
        Probe probe{scenario};
        probe.cancel = scenario == Scenario::Cancel;
        std::thread automation([&]() {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            for (int i = 0; i < 100 && !probe.done; ++i) {
                Sleep(100);
                EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&probe));
                if (probe.acted) break;
            }
            if (!probe.acted && !probe.done) {
                // Timeout closes the dialog but must not turn a failure into a pass.
                probe.cancel = true;
                EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&probe));
                probe.acted = false;
            }
            if (SUCCEEDED(initialized)) CoUninitialize();
        });
        const auto selection = SelectAlbumFolder(nullptr, QStringLiteral("Album 原生目录选择验证"), folder);
        probe.done = true;
        automation.join();
        const bool expectsEmpty = scenario == Scenario::Cancel || scenario == Scenario::SelectedImage;
        const QString expected = scenario == Scenario::SelectedFolder ? childFolder : folder;
        const bool passed = probe.acted && selection.error.isEmpty() && (expectsEmpty
            ? selection.path.isEmpty() : QDir::cleanPath(selection.path) == QDir::cleanPath(expected));
        qInfo() << (passed ? "PASS" : "FAIL") << "native scenario" << static_cast<int>(scenario)
                << selection.path << selection.error;
        if (!passed) ++failures;
    }
    return failures ? 1 : 0;
}

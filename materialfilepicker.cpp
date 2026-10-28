#include "materialfilepicker.h"
#include "materialsavedialog.h"
#include "imageexportservice.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QWidget>
#include <QScopedValueRollback>
#include <vector>
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace {
using Microsoft::WRL::ComPtr;
QString shellPath(IShellItem *item)
{
    PWSTR value = nullptr;
    if (!item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) return {};
    const QString path = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return path;
}
class SaveEvents final : public IFileDialogEvents
{
public:
    SaveEvents(const QString &source, const QString &initial, const QStringList &extensions)
        : source(source), lastName(QFileInfo(initial).fileName()), extensions(extensions)
    {
        automatic = QRegularExpression("^" + QRegularExpression::escape(QFileInfo(source).completeBaseName())
            + "_crop_[0-9]+$").match(QFileInfo(initial).completeBaseName()).hasMatch();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IFileDialogEvents)) return E_NOINTERFACE;
        *object = static_cast<IFileDialogEvents*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = InterlockedDecrement(&references);
        if (!count) delete this;
        return count;
    }
    bool update(IFileDialog *dialog)
    {
        if (!automatic || updating) return false;
        QScopedValueRollback<bool> guard(updating, true);
        PWSTR value = nullptr;
        if (FAILED(dialog->GetFileName(&value))) return false;
        const QString current = QString::fromWCharArray(value);
        CoTaskMemFree(value);
        // 用户改过名字后，停止自动命名；系统随格式变化修改扩展名不算手动改名。
        if (!current.isEmpty() && QFileInfo(current).completeBaseName() != QFileInfo(lastName).completeBaseName()) {
            automatic = false;
            return false;
        }
        ComPtr<IShellItem> folder;
        if (FAILED(dialog->GetFolder(&folder))) return false;
        const QString directory = shellPath(folder.Get());
        if (directory.isEmpty()) return false;
        UINT index = 1;
        dialog->GetFileTypeIndex(&index);
        const QString extension = extensions.value(int(index) - 1, "png");
        const QString next = QFileInfo(MaterialSaveDialog::suggestedPath(source, directory, extension)).fileName();
        dialog->SetDefaultExtension(reinterpret_cast<LPCWSTR>(extension.utf16()));
        if (next == current) return false;
        if (FAILED(dialog->SetFileName(reinterpret_cast<LPCWSTR>(next.utf16())))) return false;
        lastName = next;
        return true;
    }
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog *dialog) override
    { return update(dialog) ? S_FALSE : S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog *, IShellItem *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog *dialog) override { update(dialog); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog *, IShellItem *, FDE_SHAREVIOLATION_RESPONSE *response) override
    { if (response) *response = FDESVR_DEFAULT; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog *dialog) override { update(dialog); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog *dialog, IShellItem *, FDE_OVERWRITE_RESPONSE *response) override
    {
        const bool changed = update(dialog);
        if (response) *response = changed ? FDEOR_REFUSE : FDEOR_DEFAULT;
        return S_OK;
    }
private:
    LONG references = 1;
    QString source, lastName;
    QStringList extensions;
    bool automatic = false;
    bool updating = false;
};
}

MaterialFilePicker::Result MaterialFilePicker::choose(const QString &sourcePath, const QString &initialPath, QWidget *parent)
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(apartment)) return {{}, QStringLiteral("无法初始化 Windows 文件对话框。")};
    struct ApartmentExit { ~ApartmentExit() { CoUninitialize(); } } apartmentExit;
    ComPtr<IFileSaveDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) return {{}, QStringLiteral("无法创建 Windows 保存对话框。")};
    const QStringList supported = ImageExportService::supportedExtensions();
    QStringList extensions;
    std::vector<COMDLG_FILTERSPEC> filters;
    if (supported.contains("png")) { extensions << "png"; filters.push_back({L"PNG 图像 (*.png)", L"*.png"}); }
    if (supported.contains("jpg")) { extensions << "jpg"; filters.push_back({L"JPEG 图像 (*.jpg; *.jpeg)", L"*.jpg;*.jpeg"}); }
    if (supported.contains("bmp")) { extensions << "bmp"; filters.push_back({L"BMP 图像 (*.bmp)", L"*.bmp"}); }
    if (filters.empty()) return {{}, QStringLiteral("没有可用的图像编码器。")};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT | FOS_NOCHANGEDIR);
    dialog->SetTitle(L"素材保存位置");
    dialog->SetFileTypes(UINT(filters.size()), filters.data());
    const QFileInfo initial(initialPath);
    QString extension = initial.suffix().toLower();
    if (extension == "jpeg") extension = "jpg";
    const int index = extensions.indexOf(extension);
    dialog->SetFileTypeIndex(index < 0 ? 1 : UINT(index + 1));
    extension = extensions.value(index < 0 ? 0 : index);
    dialog->SetDefaultExtension(reinterpret_cast<LPCWSTR>(extension.utf16()));
    ComPtr<IShellItem> folder;
    const QString directory = QDir::toNativeSeparators(initial.absolutePath());
    if (SUCCEEDED(SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(directory.utf16()), nullptr, IID_PPV_ARGS(&folder))))
        dialog->SetFolder(folder.Get());
    const QString name = initial.fileName();
    dialog->SetFileName(reinterpret_cast<LPCWSTR>(name.utf16()));
    ComPtr<SaveEvents> events;
    events.Attach(new SaveEvents(sourcePath, initialPath, extensions));
    DWORD cookie = 0;
    if (FAILED(dialog->Advise(events.Get(), &cookie))) return {{}, QStringLiteral("无法设置保存文件命名逻辑。")};
    events->update(dialog.Get());
    result = dialog->Show(parent ? reinterpret_cast<HWND>(parent->winId()) : nullptr);
    dialog->Unadvise(cookie);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if (FAILED(result)) return {{}, QStringLiteral("Windows 保存对话框打开失败（%1）。").arg(quint32(result), 8, 16, QLatin1Char('0'))};
    ComPtr<IShellItem> selected;
    if (FAILED(dialog->GetResult(&selected))) return {{}, QStringLiteral("无法读取所选保存路径。")};
    const QString path = shellPath(selected.Get());
    return {path, path.isEmpty() ? QStringLiteral("请选择磁盘上的保存位置。") : QString()};
}

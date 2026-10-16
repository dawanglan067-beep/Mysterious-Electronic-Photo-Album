#include "albumfolderpicker.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QWidget>
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace {
using Microsoft::WRL::ComPtr;
constexpr DWORD SelectCurrentFolder = 101;
constexpr DWORD HelpText = 102;
constexpr DWORD TargetPathText = 103;

struct FolderTarget
{
    QString path;
    bool hasSelection = false;
};

class ComApartment
{
public:
    ComApartment() : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    HRESULT result;
};

QString ShellDirectory(IShellItem *item)
{
    if (!item) return {};
    SFGAOF attributes = 0;
    if (FAILED(item->GetAttributes(SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes))
        || (attributes & (SFGAO_FOLDER | SFGAO_FILESYSTEM)) != (SFGAO_FOLDER | SFGAO_FILESYSTEM)) return {};
    PWSTR text = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &text))) return {};
    const QString path = QDir::cleanPath(QString::fromWCharArray(text));
    CoTaskMemFree(text);
    const QFileInfo info(path);
    return info.isDir() && info.isReadable() ? info.absoluteFilePath() : QString();
}

FolderTarget ResolveFolderTarget(IFileDialog *dialog)
{
    ComPtr<IShellItem> item;
    if (SUCCEEDED(dialog->GetCurrentSelection(&item)) && item)
        return {ShellDirectory(item.Get()), true};
    if (SUCCEEDED(dialog->GetFolder(&item)))
        return {ShellDirectory(item.Get()), false};
    return {};
}

class FolderEvents final : public IFileDialogEvents, public IFileDialogControlEvents
{
public:
    explicit FolderEvents(IFileDialog *dialog) : _dialog(dialog) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IFileDialogEvents))
            *object = static_cast<IFileDialogEvents*>(this);
        else if (iid == __uuidof(IFileDialogControlEvents))
            *object = static_cast<IFileDialogControlEvents*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&_references); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = InterlockedDecrement(&_references);
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog *dialog) override
    {
        // Double-click/Enter may navigate a folder, but must never accept a photo.
        ComPtr<IShellItem> selected;
        if (SUCCEEDED(dialog->GetResult(&selected))) {
            if (!ShellDirectory(selected.Get()).isEmpty()) dialog->SetFolder(selected.Get());
        }
        UpdateFolder();
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog *, IShellItem *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog *) override { UpdateFolder(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog *) override { UpdateFolder(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog *, IShellItem *, FDE_SHAREVIOLATION_RESPONSE *response) override
    { if (response) *response = FDESVR_DEFAULT; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog *, IShellItem *, FDE_OVERWRITE_RESPONSE *response) override
    { if (response) *response = FDEOR_DEFAULT; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnItemSelected(IFileDialogCustomize *, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnButtonClicked(IFileDialogCustomize *, DWORD id) override
    {
        if (id != SelectCurrentFolder) return S_OK;
        const auto target = ResolveFolderTarget(_dialog);
        if (target.path.isEmpty()) {
            UpdateFolder();
            return S_OK;
        }
        selectedPath = target.path;
        const HRESULT result = _dialog->Close(S_OK);
        if (FAILED(result)) { selectedPath.clear(); SetHelp(L"无法完成选择，请重试。"); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnCheckButtonToggled(IFileDialogCustomize *, DWORD, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnControlActivating(IFileDialogCustomize *, DWORD) override { return S_OK; }
    void UpdateFolder()
    {
        ComPtr<IFileDialogCustomize> controls;
        const auto target = ResolveFolderTarget(_dialog);
        if (SUCCEEDED(_dialog->QueryInterface(IID_PPV_ARGS(&controls)))) {
            controls->SetControlState(SelectCurrentFolder, target.path.isEmpty() ? CDCS_VISIBLE
                : static_cast<CDCONTROLSTATEF>(CDCS_VISIBLE | CDCS_ENABLED));
            controls->SetControlLabel(SelectCurrentFolder, target.hasSelection
                ? L"选择此文件夹" : L"选择当前文件夹");
            const QString help = target.path.isEmpty()
                ? (target.hasSelection ? QStringLiteral("图片仅供浏览，请选择可读取的文件夹。")
                                       : QStringLiteral("请进入一个可读取的文件夹。"))
                : (target.hasSelection ? QStringLiteral("将打开选中的文件夹：")
                                       : QStringLiteral("将打开当前所在的文件夹："));
            controls->SetControlLabel(HelpText, reinterpret_cast<LPCWSTR>(help.utf16()));
            // Keep the directory name at the start so it remains visible in narrow dialogs.
            const QString detail = target.path.isEmpty() ? QStringLiteral("未选择有效文件夹")
                : QStringLiteral("%1 — %2").arg(QDir(target.path).dirName(),
                                                  QDir::toNativeSeparators(target.path));
            controls->SetControlLabel(TargetPathText, reinterpret_cast<LPCWSTR>(detail.utf16()));
        }
    }
    QString selectedPath;
private:
    void SetHelp(LPCWSTR text)
    {
        ComPtr<IFileDialogCustomize> controls;
        if (SUCCEEDED(_dialog->QueryInterface(IID_PPV_ARGS(&controls)))) controls->SetControlLabel(HelpText, text);
    }
    LONG _references = 1;
    IFileDialog *_dialog; // Valid for the entire Advise/Show/Unadvise scope.
};

QString SetupError(HRESULT result)
{
    return QStringLiteral("无法打开 Windows 文件夹浏览窗口（错误码 0x%1）。")
        .arg(quint32(result), 8, 16, QLatin1Char('0'));
}
}

AlbumFolderSelection SelectAlbumFolder(QWidget *parent, const QString &title, const QString &initialDirectory)
{
    ComApartment apartment;
    if (FAILED(apartment.result)) return {{}, SetupError(apartment.result)};
    ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) return {{}, SetupError(result)};
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(result)) result = dialog->GetOptions(&options);
    options &= ~(FOS_PICKFOLDERS | FOS_ALLOWMULTISELECT);
    if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST
        | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT);
    if (SUCCEEDED(result)) result = dialog->SetTitle(reinterpret_cast<LPCWSTR>(title.utf16()));
    if (SUCCEEDED(result)) result = dialog->SetOkButtonLabel(L"浏览所选项");
    const COMDLG_FILTERSPEC filters[] = {{L"图片文件", L"*.png;*.jpg;*.jpeg;*.bmp;*.webp"}, {L"所有文件", L"*.*"}};
    if (SUCCEEDED(result)) result = dialog->SetFileTypes(2, filters);
    ComPtr<IFileDialogCustomize> controls;
    if (SUCCEEDED(result)) result = dialog.As(&controls);
    if (SUCCEEDED(result)) result = controls->AddPushButton(SelectCurrentFolder, L"选择当前文件夹");
    if (SUCCEEDED(result)) result = controls->MakeProminent(SelectCurrentFolder);
    if (SUCCEEDED(result)) result = controls->AddText(HelpText, L"请进入相册目录，再点击“选择当前文件夹”。");
    if (SUCCEEDED(result)) result = controls->AddText(TargetPathText, L"正在读取目录…");
    if (FAILED(result)) return {{}, SetupError(result)};
    QString start = initialDirectory;
    if (!QFileInfo(start).isDir()) start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!QFileInfo(start).isDir()) start = QDir::homePath();
    ComPtr<IShellItem> initial;
    const QString native = QDir::toNativeSeparators(QFileInfo(start).absoluteFilePath());
    result = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, IID_PPV_ARGS(&initial));
    if (SUCCEEDED(result)) result = dialog->SetFolder(initial.Get());
    if (FAILED(result)) return {{}, SetupError(result)};
    ComPtr<FolderEvents> events;
    events.Attach(new FolderEvents(dialog.Get()));
    DWORD cookie = 0;
    result = dialog->Advise(events.Get(), &cookie);
    if (FAILED(result)) return {{}, SetupError(result)};
    events->UpdateFolder();
    result = dialog->Show(parent ? reinterpret_cast<HWND>(parent->window()->winId()) : nullptr);
    dialog->Unadvise(cookie);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if (FAILED(result)) return {{}, SetupError(result)};
    if (!QFileInfo(events->selectedPath).isDir())
        return {{}, QStringLiteral("所选文件夹已不存在，请重新选择。")};
    return {events->selectedPath, {}};
}

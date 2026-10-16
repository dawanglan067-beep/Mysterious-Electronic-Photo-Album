#include "removeprodialog.h"
#include "ui_removeprodialog.h"
#include <QPushButton>

RemoveProDialog::RemoveProDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RemoveProDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("关闭相册"));
    ui->checkBox->setChecked(false);

    // 关键防误触保护：明确将初始键盘焦点设置在“取消”按钮上，
    // 坚决防止 QCheckBox 成为初始获焦控件导致用户习惯性敲击空格/回车误触勾选！
    if (auto *btnCancel = ui->buttonBox->button(QDialogButtonBox::Cancel))
    {
        btnCancel->setFocus();
        btnCancel->setDefault(true);
    }
    if (auto *btnOk = ui->buttonBox->button(QDialogButtonBox::Ok))
    {
        btnOk->setDefault(false);
    }
}

RemoveProDialog::~RemoveProDialog()
{
    delete ui;
}

bool RemoveProDialog::IsRemoved()
{
    return ui->checkBox->isChecked();
}

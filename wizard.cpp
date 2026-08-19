#include "wizard.h"
#include "ui_wizard.h"

Wizard::Wizard(QWidget *parent)
    : QWizard(parent)
    , ui(new Ui::Wizard)
{
    ui->setupUi(this);
    // 设置为经典向导样式，禁用 Windows 原生白色 Aero 模板，让暗色 QSS 完全生效
    setWizardStyle(QWizard::ClassicStyle);
}

Wizard::~Wizard()
{
    delete ui;
}

void Wizard::done(int result)
{
    if(result==QDialog::Rejected)
    {
        return QWizard::done(result);
    }
    QString name,path;
    //拿到用户的信息
    ui->wizardPage1->GetProSettings(name,path);
    emit SigProSettings(name,path);
    QWizard::done(result);
}

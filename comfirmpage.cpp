#include "comfirmpage.h"
#include "ui_comfirmpage.h"

ComfirmPage::ComfirmPage(QWidget *parent)
    : QWizardPage(parent)
    , ui(new Ui::ComfirmPage)
{
    ui->setupUi(this);
}

ComfirmPage::~ComfirmPage()
{
    delete ui;
}

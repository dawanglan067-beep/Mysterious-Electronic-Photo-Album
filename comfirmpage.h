#ifndef COMFIRMPAGE_H
#define COMFIRMPAGE_H

#include <QWizardPage>

namespace Ui {
class ComfirmPage;
}

class ComfirmPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit ComfirmPage(QWidget *parent = nullptr);
    ~ComfirmPage();

private:
    Ui::ComfirmPage *ui;
};

#endif // COMFIRMPAGE_H

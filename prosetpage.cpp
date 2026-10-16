#include "prosetpage.h"
#include "ui_prosetpage.h"
#include<QLineEdit>
#include<QDir>
#include<QFileDialog>
ProSetPage::ProSetPage(QWidget *parent)
    : QWizardPage(parent)
    , ui(new Ui::ProSetPage)
{
    ui->setupUi(this);
    //数据共享和实时动态监测
    registerField("proPath",ui->lineEdit_2);
    registerField("proName*",ui->lineEdit);
    //做连接
    connect(ui->lineEdit,&QLineEdit::textChanged,this,&ProSetPage::completeChanged);
    connect(ui->lineEdit_2,&QLineEdit::textChanged,this,&ProSetPage::completeChanged);
    QString curPath=QDir::currentPath();//获取当前程序运行时的“工作目录
    ui->lineEdit_2->setText(curPath);//默认路径
    //设置光标
    ui->lineEdit_2->setCursorPosition(ui->lineEdit_2->text().size());
    ui->lineEdit_2->setClearButtonEnabled(true);
     ui->lineEdit->setClearButtonEnabled(true);

}

ProSetPage::~ProSetPage()
{
    delete ui;
}

void ProSetPage::GetProSettings(QString &name, QString &path)
{
    name=ui->lineEdit->text();
    path=ui->lineEdit_2->text();

}

bool ProSetPage::isComplete() const
{
    if(ui->lineEdit->text()==""||ui->lineEdit_2->text()=="")
    {
        return false;

    }
    QDir dir(ui->lineEdit_2->text());
    if(!dir.exists())
    {
        ui->tips->setText("project path is not exists");
            return false;
    }
    //判断路径
    QString absFliePath=dir.absoluteFilePath(ui->lineEdit->text());
    QDir dis_dir(absFliePath);
    if(dis_dir.exists())
    {
        ui->tips->setText("project has exists,change path or name!");
        return false;

    }
    ui->tips->setText("");
    return QWizardPage::isComplete();
}

void ProSetPage::on_pushButton_clicked()
{
    QFileDialog file_dialog;
    file_dialog.setFileMode(QFileDialog::Directory);
    file_dialog.setWindowTitle("选择导入的文件夹");
    //获取应用程序当前路径
    auto path=QDir::currentPath();
    file_dialog.setDirectory(path);
    //设置对话框内显示的文件列表为详细信息视图
    file_dialog.setViewMode(QFileDialog::Detail);
    //存储用户最终保存的路径
    QStringList fileNames;
    //等待用户交互
    if(file_dialog.exec())
    {
        //获取用户选择的完整的路径列表
        fileNames=file_dialog.selectedFiles();
    }
    //判断用户是否选择任何有效路径
    if(fileNames.length()<=0)
    {
        return;
    }
    //选取用户选择的第一个路径，并显示到界面的单行输入框
    QString import_path=fileNames.at(0);
    ui->lineEdit_2->setText(import_path);

}


#pragma once
#include <QDialog>
#include <QStringList>
#include "duplicatephotofinder.h"

class QListWidget;

// 只拥有路径、匹配结果与选择，不持有树节点，也不直接删除文件。
class DuplicateReviewDialog : public QDialog
{
    Q_OBJECT
public:
    DuplicateReviewDialog(const QVector<DuplicatePhotoFinder::PhotoHash> &photos,
                          const QVector<DuplicatePhotoFinder::Match> &matches, QWidget *parent = nullptr);
    QStringList SelectedPaths() const;
private:
    QListWidget *_list;
};
